# Custom FBO Preview Renderer + Collision Mask Plan

Replace the ImGui-based markdown preview with a fully custom GL renderer that draws text as glyph quads, blits inline model/script FBO textures, and builds a collision mask — all into a single preview FBO. ImGui widgets (checkboxes, buttons, links) are overlaid on top of the preview FBO while there are place holder boxes for these items rendered to the FBO. Particles sample the collision mask for cross-line interaction. The collision mask is exposed to Lua scripts.

The existing `TextEffectsOverlay` alpha mask pipeline already renders glyph quads to an FBO with a custom shader. This plan extends that into a full RGBA preview renderer with layout, font selection, word-wrap, and embedded content support.

---

## Architecture

```
┌─ ImGui Frame ──────────────────────────────────────────────┐
│  BeginChild("VaultPreviewRight")                           │
│    ┌─ Custom GL Rendering (no ImGui) ────────────────────┐ │
│    │  Layout engine processes md4c callbacks             │ │
│    │  Text → glyph quads into preview FBO                │ │
│    │  ModelViewer::drawGL() → blit to preview FBO        │ │
│    │  LuaEngine::renderCanvasFrame() → blit to FBO       │ │
│    │  Images → textured quads into preview FBO           │ │
│    │  Interactive → placeholder boxes rendered to FBO    │ │
│    │                                                     │ │
│    │  Build collision mask from all element rects        │ │
│    │  glReadPixels collision mask → CPU buffer           │ │
│    │  Particles sample collision mask for physics        │ │
│    └─────────────────────────────────────────────────────┘ │
│                                                            │
│    Display previewFBO texture via ImGui::Image()           │
│    Overlay real ImGui widgets on top of placeholder boxes  │
│  EndChild()                                                │
└────────────────────────────────────────────────────────────┘
```

---

## Step 1: Create `PreviewRenderer` class

New files: `include/PreviewRenderer.hpp`, `src/PreviewRenderer.cpp`.

This class owns:

- **Preview FBO** (`GL_RGBA8` + depth/stencil RBO) — sized to the preview region, resized via the existing `ensureTexture()` pattern from `TextEffectsOverlay.cpp` (line 142).
- **Collision mask FBO** (`GL_R8`) — same dimensions, receives solid-filled quads for every rendered element.
- **Preview shader** — extends the existing alpha mask shader (vertex format: `[pos.xy, uv.xy]`, R8 output) to an RGBA shader with color support. New vertex format: `[pos.xy, uv.xy, color.rgba]` (32 bytes/vert).
- **Collision shader** — the existing alpha mask shader as-is (R8 output, samples font atlas alpha).
- **Glyph vertex buffer** (VAO/VBO) — built per frame from the layout pass.
- **Collision vertex buffer** — filled quads for every element (text, images, embeds).
- **Collision readback buffer** — CPU-side `std::vector<uint8_t>` populated via `glReadPixels` after mask render for O(1) particle sampling.

### Shaders

**Preview vertex shader** (extends existing `s_glyphVS_full`):
```glsl
#version 330 core
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
uniform vec2 uDocSize;
out vec2 v_uv;
out vec4 v_color;
void main() {
    vec2 ndc = (in_pos / uDocSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = in_uv;
    v_color = in_color;
}
```

**Preview fragment shader**:
```glsl
#version 330 core
uniform sampler2D uFontAtlas;
in vec2 v_uv;
in vec4 v_color;
out vec4 fragColor;
void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    fragColor = vec4(v_color.rgb, v_color.a * alpha);
}
```

**Collision shaders** — reuse existing `s_glyphVS_full` + `s_glyphFS` unchanged (R8 alpha mask from font atlas).

**Embed quad shader** (for images, model textures, script canvas textures):
```glsl
// vertex: same as preview VS but with full UV pass-through
// fragment:
#version 330 core
uniform sampler2D uTexture;
in vec2 v_uv;
out vec4 fragColor;
void main() {
    fragColor = texture(uTexture, v_uv);
}
```

### Key API

```cpp
class PreviewRenderer {
public:
    void beginFrame(float scrollY, float viewportW, float viewportH);
    void endFrame(float dt);

    ImTextureID getPreviewTexture() const;
    ImTextureID getCollisionTexture() const;

    // CPU-side collision sampling (uses readback buffer)
    bool sampleCollision(float docX, float docY) const;
    float getCollisionValue(float docX, float docY) const; // 0.0–1.0

    // For Lua / external access
    GLuint getCollisionMaskTextureID() const;
    GLuint getPreviewTextureID() const;
    const uint8_t* getCollisionBuffer() const;
    int getCollisionWidth() const;
    int getCollisionHeight() const;

    // Layout output for ImGui overlay
    struct OverlayWidget {
        enum Type { Checkbox, LinkButton, ModelInteract, ScriptInteract };
        Type type;
        ImVec2 docPos;     // document-space position
        ImVec2 size;       // widget size
        // Type-specific data
        bool checkboxChecked;
        size_t sourceOffset;  // byte offset in source for checkbox toggle
        std::string linkUrl;
        std::string embedSrc;
    };
    const std::vector<OverlayWidget>& getOverlayWidgets() const;
};
```

---

## Step 2: Build the layout engine

New section within `PreviewRenderer`. Replaces ImGui's layout with a custom document-space layout pass.

The layout engine processes md4c callbacks (same `enter_block`/`leave_block`/`enter_span`/`leave_span`/`textcb` pattern from `MarkdownText.cpp`) but instead of calling `ImGui::TextWrapped()`, it:

1. Maintains a cursor `(curX, curY)` in document space.
2. Uses `ImFont::CalcWordWrapPositionA()` to find line break positions — this is ImGui's word-wrap algorithm, usable without any ImGui widget context.
3. Walks UTF-8 text, calls `font->FindGlyph(codepoint)` for each character, emits 6 vertices per glyph quad `[pos.xy, uv.xy, color.rgba]` into the glyph buffer.
4. Applies font selection (bold/italic/mono) by switching the active `ImFont*` — fonts are still loaded via `ImFontAtlas`, we just use their glyph data directly.
5. For each glyph, also emits a filled quad into the collision buffer.

### Block layout rules

Matching the current ImGui-based renderer behavior:

| Block Type | Layout Action |
|---|---|
| **Headers** | Scale glyph sizes by `1.4 - (level-1)*0.1` (min 0.9), use bold font. On leave: restore scale, add 6px vertical spacing. |
| **Lists (UL/OL)** | Offset `curX` by `fontSize * 1.5` per nesting level. |
| **List Items (LI)** | OL: render number glyphs. UL: render bullet glyph. Start wrapped text after bullet. |
| **Paragraphs** | Record wrap width from available content region. Reset `curX` on leave. |
| **Code Blocks** | Render background-fill quad (dark `#141414`), use monospace font, no word-wrap. |
| **Blockquotes** | Indent by `fontSize * 1.0`, tint text color grey `(0.6, 0.6, 0.6)`. |
| **Tables** | Measure column widths from content, render cell text with border line quads. Header cells use bold font. |
| **HR** | Render a thin horizontal line quad spanning the content width. |

### Text rendering

For each text callback:
```
1. Get active font (bold/italic/mono from span stack)
2. Get wrap width from current layout context
3. Loop:
   a. Call font->CalcWordWrapPositionA(scale, text, text_end, wrapWidth) → breakPos
   b. For each char from text to breakPos:
      - font->FindGlyph(codepoint) → glyph
      - Emit 6 vertices: pos = (curX + glyph.X0*scale, curY + glyph.Y0*scale, ...) with UVs and current color
      - Emit filled quad to collision buffer
      - Advance curX by glyph.AdvanceX * scale
   c. Line break: curX = indentX, curY += lineHeight
   d. text = breakPos, continue loop
```

### Definition list heuristic

Same logic as current `renderText()` — detect `Term: definition` pattern and `: definition` prefix. Render term in bold, definition in normal weight.

### Task list checkboxes

Detect `[ ]` / `[x]` markers. Render a **placeholder box** to the FBO at the checkbox position (a small filled rect matching the checkbox size, e.g. a bordered square with check/empty state drawn as simple geometry). This placeholder is visible in the preview and written to the collision mask. Additionally, record an `OverlayWidget{Checkbox, ...}` with the position and state so a real ImGui `Checkbox` widget is overlaid on top for actual click interaction. Advance the cursor past the checkbox space.

### Links

Render link text normally to the FBO (with link-colored glyphs). Record an `OverlayWidget{LinkButton, ...}` so a real ImGui button is overlaid on top for click handling and hover cursor.

---

## Step 3: Handle inline embeds

When the layout encounters an `MD_SPAN_IMG`:

### Images (`vault://Assets/`, http URLs)

The image is already loaded as a GL texture (the existing async loading and `GetDynamicTexture()` in `MarkdownText.cpp` caches these).

1. Look up the cached texture via `GetDynamicTexture(key)`.
2. If loaded: draw a textured quad into the preview FBO at `(curX, curY)` with computed dimensions using the embed shader. Add a collision rect of the same size.
3. If not loaded: draw a placeholder rect (grey with "Loading..." text). Enqueue the async load (same as current code).
4. Advance `curY` past the image.

### 3D Models (`vault://Assets/*.glb` etc.)

`ModelViewer::drawGL(w, h)` in `ModelViewer.cpp` already renders to its own FBO and returns a texture.

1. Call `ModelViewer::drawGL(w, h)` to render the model to its FBO.
2. Draw/blit the model's FBO texture as a quad into the preview FBO at `(curX, curY)`.
3. Add a collision rect of the same size.
4. The model texture in the FBO acts as the visible placeholder — what the user sees is the actual rendered model.
5. Record an `OverlayWidget{ModelInteract, pos, size, src}` so a real ImGui widget is overlaid on top to handle orbit/pan/zoom input.
6. Advance `curY` past the model viewer.

### Lua Scripts (`vault://Scripts/`)

`LuaEngine::renderCanvasFrame()` already returns a GL texture ID.

1. Call `eng->renderCanvasFrame(embedID, width, height, dt)`.
2. Draw the canvas texture as a quad into the preview FBO (this is the visible placeholder — the rendered script output).
3. Add a collision rect.
4. Record an `OverlayWidget{ScriptInteract, pos, size, scriptName}` so a real ImGui widget is overlaid on top for mouse event forwarding.
5. Advance `curY` past the script canvas.

### World Maps (`vault://World/`)

Same approach as Lua scripts — the world map renderers (`mercatorMap`, `globeMap`) already render to screen via ImGui. These need to be modified to render to their own FBOs, then blitted into the preview FBO. Or: render them as Lua canvas-style FBO textures.

---

## Step 4: Render the FBO

In `PreviewRenderer::endFrame(float dt)`:

1. **Lazy-init** GL resources (FBOs, textures, shaders, VAO/VBO) on first call. Resize if viewport changed.
2. **Save GL state** — current FBO, viewport, blend, depth, scissor (same pattern as `TextEffectsOverlay::renderAlphaMask()`).

### Preview pass

3. **Bind preview FBO**, set viewport to `(0, 0, fbW, fbH)`.
4. **Clear** to background color (dark theme: `#1A1A1A`).
5. **Apply text effects** to the glyph vertex data before upload — call `TextEffectSystem::applyEffect()` on the raw vertex arrays. Since we own the vertex buffer, effects modify positions/colors directly. The collision mask is already available for particles to sample.
6. **Bind preview shader**, set `uDocSize` uniform, bind font atlas texture.
7. **Upload and draw** glyph vertex buffer (all text quads in one draw call).
8. For each embed texture (image/model/script):
   - Bind embed shader, bind embed texture, draw positioned quad.
9. **Draw background-fill quads** (code blocks, table backgrounds).
10. **Draw line quads** (HR separators, table borders).

### Collision mask pass

11. **Bind collision mask FBO**, clear to 0.
12. **Bind collision shader**, set `uDocSize` uniform, bind font atlas texture.
13. **Upload and draw** collision vertex buffer (text glyph + embed rects + widget reserved rects).
14. **`glReadPixels`** the R8 mask into the CPU readback buffer.

### Particle pass

15. **Update particles** — each particle system samples the collision readback buffer for physics. Fire deflects around obstacles, snow accumulates on surfaces, drips splatter on impact.
16. **Draw particle quads** into the preview FBO (re-bind preview FBO, draw particles as colored quads/circles).

### Restore

17. **Restore** saved GL state (FBO, viewport, blend, depth, scissor).

---

## Step 5: Display and overlay

In `Vault.hpp` preview panel code (replacing current lines 2770–2886):

```cpp
ImGui::BeginChild("VaultPreviewRight", ImVec2(0, mainHeight), true);

// 1. Run layout + FBO render
float scrollY = ImGui::GetScrollY();
ImVec2 contentOrigin = ImGui::GetCursorScreenPos();
float vpW = ImGui::GetContentRegionAvail().x;
float vpH = ImGui::GetWindowHeight();

previewRenderer.beginFrame(scrollY, vpW, vpH);
previewRenderer.processMarkdown(currentContent, this); // 'this' = Vault*
previewRenderer.endFrame(ImGui::GetIO().DeltaTime);

// 2. Display the preview texture (flipped Y for GL → ImGui coords)
ImGui::Image(previewRenderer.getPreviewTexture(),
             ImVec2(vpW, vpH),
             ImVec2(0, 1), ImVec2(1, 0));  // flip Y

// 3. Overlay real ImGui widgets on top of their placeholder boxes in the FBO
//    The FBO already has visible placeholder graphics at these positions.
//    The ImGui widgets here provide the actual interactivity (clicks, hover, input).
for (auto &w : previewRenderer.getOverlayWidgets()) {
    ImVec2 screenPos(contentOrigin.x + w.docPos.x,
                     contentOrigin.y + w.docPos.y - scrollY);

    switch (w.type) {
    case OverlayWidget::Checkbox: {
        // Real ImGui Checkbox overlaid on top of the FBO placeholder box
        ImGui::SetCursorScreenPos(screenPos);
        ImGui::PushID((void*)(intptr_t)w.sourceOffset);
        bool v = w.checkboxChecked;
        if (ImGui::Checkbox("", &v)) {
            replaceCurrentContentRange(w.sourceOffset, 3,
                                       v ? "[x]" : "[ ]");
        }
        ImGui::PopID();
        break;
    }
    case OverlayWidget::LinkButton: {
        // Transparent button overlaid on FBO-rendered link text
        ImGui::SetCursorScreenPos(screenPos);
        ImGui::InvisibleButton("link", w.size);
        if (ImGui::IsItemClicked())
            openUrl(w.linkUrl);
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        break;
    }
    case OverlayWidget::ModelInteract: {
        // Transparent input region over FBO-rendered model texture
        ImGui::SetCursorScreenPos(screenPos);
        ImGui::InvisibleButton("model", w.size);
        // Forward mouse input to ModelViewer for orbit/pan/zoom
        break;
    }
    case OverlayWidget::ScriptInteract: {
        // Transparent input region over FBO-rendered script canvas
        ImGui::SetCursorScreenPos(screenPos);
        ImGui::InvisibleButton("script", w.size);
        // Forward mouse events to LuaEngine via callOnCanvasEvent
        break;
    }
    }
}

ImGui::EndChild();
```

---

## Step 6: Particle collision sampling

Modify all particle systems in `TextEffectSystem.cpp`:

- `applyFireEmit` (~line 300)
- `applyFairyDust` (~line 350)
- `applySnow` (~line 600)
- `applyBubbles` (~line 700)
- `applyDrip` (~line 900)

### Collision sampler interface

```cpp
struct CollisionSampler {
    const uint8_t* buffer;
    int width, height;

    bool solid(float x, float y) const {
        int ix = (int)x, iy = (int)y;
        if (ix < 0 || iy < 0 || ix >= width || iy >= height) return false;
        return buffer[iy * width + ix] > 0;
    }

    // Sample surface normal by checking neighboring pixels
    ImVec2 surfaceNormal(float x, float y) const {
        float dx = (solid(x+1,y) ? 1.0f : 0.0f) - (solid(x-1,y) ? 1.0f : 0.0f);
        float dy = (solid(x,y+1) ? 1.0f : 0.0f) - (solid(x,y-1) ? 1.0f : 0.0f);
        float len = sqrtf(dx*dx + dy*dy);
        if (len < 0.001f) return ImVec2(0, -1);
        return ImVec2(-dx/len, -dy/len);
    }
};
```

### Per-particle-type collision response

| System | Collision Response |
|---|---|
| **Fire** | Deflect laterally along surface normal. Adds turbulence creating flame curling around text on other lines. |
| **FairyDust** | Bounce off surfaces with damping. Sparkles swirl around obstacles. |
| **Snow** | Stick on top surfaces (solid below, clear above). Accumulate — flakes pile up on text below. |
| **Bubbles** | Deflect sideways. Bubbles flow around obstacles like water. |
| **Drip** | Splatter on impact — spawn small child particles, stop vertical movement. Blood drops hit text below and spread. |

### Passing the sampler

Add a `const CollisionSampler*` parameter to `TextEffectSystem::applyEffect()`:

```cpp
void applyEffect(ImDrawList *dl, int vtxStart, int vtxEnd,
                 ImTextureID atlasID, float time,
                 const TextEffectDef &effect,
                 const CollisionSampler *collision = nullptr);
```

When `collision != nullptr`, particle update functions query it before advancing positions. When `nullptr` (backwards compatibility), particles move freely as before.

---

## Step 7: Expose to Lua

In `LuaCanvasBindings.cpp`, add new bindings:

```lua
-- Sample collision at document coordinates
local solid = canvas.sampleCollision(x, y)       -- returns true/false

-- Get collision value (0.0–1.0)
local val = canvas.getCollisionValue(x, y)        -- returns float

-- Get texture IDs for GPU-side sampling in custom shaders
local maskTex = canvas.getCollisionMaskTexture()  -- GLuint
local previewTex = canvas.getPreviewTexture()     -- GLuint

-- Get collision buffer dimensions
local w, h = canvas.getCollisionSize()
```

This lets Lua scripts create their own particle/physics systems that interact with all document content — text, images, models, other scripts.

---

## Step 8: Merge with existing TextEffectsOverlay

The existing `TextEffectsOverlay` (`include/TextEffectsOverlay.hpp`, `src/TextEffectsOverlay.cpp`) becomes largely redundant:

| Current Component | Replacement |
|---|---|
| Alpha mask FBO (`m_alphaMaskFBO`) | Collision mask FBO in `PreviewRenderer` |
| Glyph recording (`addEffectRegion`) | Layout engine's glyph buffer |
| Output FBO (`m_outputFBO`) | Preview FBO in `PreviewRenderer` |
| CL particle pipeline | Re-point CL kernels to sample the new collision mask |
| `EffectGlyphInfo` / `EffectRegion` types | Still useful for effect system data flow |

### Migration path

1. First: build `PreviewRenderer` alongside `TextEffectsOverlay` — both work independently.
2. Once `PreviewRenderer` is verified: redirect `TextEffectsOverlay`'s CL kernels to sample the collision mask instead of the alpha mask.
3. Finally: remove the alpha mask FBO, glyph recording, and output FBO from `TextEffectsOverlay`. Keep only the CL particle pipeline, re-pointed to the new textures.

---

## Existing building blocks

| Component | Status | File |
|---|---|---|
| Glyph quad rendering to FBO | Done | `TextEffectsOverlay.cpp` `renderAlphaMask()` |
| Vertex format `[pos.xy, uv.xy]` | Done | `TextEffectsOverlay.cpp` lines 130–138 |
| FBO create/resize (`ensureTexture`) | Done | `TextEffectsOverlay.cpp` line 142 |
| GL state save/restore | Done | `TextEffectsOverlay.cpp` `renderAlphaMask()` lines 487–529 |
| GLSL 330 glyph shaders | Done | `TextEffectsOverlay.cpp` lines 17–55 |
| ImFont glyph lookup | Done | used in `MarkdownText.cpp` `renderText()` |
| Word-wrap algorithm | Available | `ImFont::CalcWordWrapPositionA()` |
| Text measurement | Available | `ImFont::CalcTextSizeA()` |
| md4c parser + callbacks | Done | `MarkdownText.cpp` |
| ModelViewer FBO rendering | Done | `ModelViewer.cpp` `drawGL()` |
| LuaEngine FBO rendering | Done | `LuaEngine.cpp` `renderCanvasFrame()` |
| Image async loading + caching | Done | `MarkdownText.cpp` + `Icons.hpp` |
| Text effect vertex manipulation | Done | `TextEffectSystem.cpp` `applyEffect()` |
| CL/GL interop particle pipeline | Done | `TextEffectsOverlay.cpp` |

---

## Verification

1. Render a document with mixed content (headers, paragraphs, lists, code blocks, images, 3D models, Lua scripts, task lists, tables). Compare visually against the old ImGui renderer.
2. Toggle a debug collision mask overlay (draw the mask as translucent red over the preview) — confirm all elements appear as solid regions.
3. Test `<effect preset=fire>` on line 1 with text on line 3 — embers should deflect around line 3 text.
4. Test `<effect preset=snow>` — flakes should accumulate on top of text/images below.
5. Test checkboxes still toggle, link clicks still work, model viewers still orbit.
6. Test Lua `canvas.sampleCollision(x, y)` returns correct values.
7. Profile: target < 2ms total for layout + FBO render + collision readback.

---

## Key decisions

- Custom GL text renderer using `ImFont` glyph data and `CalcWordWrapPositionA()` — avoids reimplementing font loading while owning the geometry.
- Collision mask as `GL_R8` with `glReadPixels` CPU readback — simple, fast enough for per-particle sampling at ~65K particles.
- Placeholder boxes rendered into the FBO for all interactive elements (checkboxes, links, embeds) — ensures they are visible in the preview and present in the collision mask. Real ImGui widgets are overlaid on top of these placeholders for actual interactivity — avoids reimplementing input handling, focus, hover states.
- Text effects applied to vertex data before GPU upload rather than post-hoc ImDrawList manipulation — cleaner, can access collision data during effect application.
- Existing CL pipeline migrated to sample collision mask, not rewritten.
