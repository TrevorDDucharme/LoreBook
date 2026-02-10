## Plan: Per-Character Text Effects System for Markdown Renderer

**TL;DR**: Add a custom HTML tag interception layer to the md4c-based markdown renderer that parses effect tags (`<pulse>`, `<glow>`, `<shake>`, `<particle>`, `<sparkle>`, `<rainbow>`, and generic `<effect type=...>`), maintains a composable effect stack, and replaces `TextWrapped()` with a per-character `ImDrawList::AddText()` renderer that applies positional, color, and overlay effects to each glyph independently. Persistent particle state (fire, sparkle) stored in `static` maps keyed by source byte offset, driven by `ImGui::GetTime()` and `ImGui::GetIO().DeltaTime`.

---

**Steps**

### 1. Tag Parser — intercept `MD_TEXT_HTML` in `textcb`

Modify `textcb` in MarkdownText.cpp to check the `type` parameter (currently ignored). When `type == MD_TEXT_HTML`:
- Parse the tag string (e.g. `<pulse cycle=500ms>`, `</glow>`, `<effect type=fire lifetime=1s>`)
- Write a small attribute parser that extracts tag name and key=value pairs (handle both quoted and unquoted values)
- On opening tag → push an `EffectState` onto a new `std::stack<EffectState> effects` member of `MD4CRenderer`
- On closing tag → pop the matching `EffectState`
- For `<effect type=X ...>` syntax, normalize to the same internal representation as `<X ...>`
- Do **not** call `renderText()` for HTML tag text — consume it silently

Add to `MD4CRenderer` struct:
```
struct EffectState { 
    enum Type { Pulse, Glow, Shake, Fire, Sparkle, Rainbow };
    Type type;
    float cycleSec;       // pulse cycle period
    float intensity;      // shake amplitude in px
    ImVec4 color;         // glow color
    float radius;         // glow radius
    float lifetimeSec;    // particle lifetime
    float speed;          // rainbow hue rotation speed
};
std::stack<EffectState> effects;
```

### 2. Per-Character Text Renderer

Add a new method `renderTextWithEffects()` to `MD4CRenderer` that replaces `TextWrapped()` when the effect stack is non-empty:

- **UTF-8 iteration**: Walk the string character-by-character using ImGui's `ImTextCharFromUtf8()` to get codepoints and byte lengths
- **Word-wrap logic**: Track current X position across the available content width (`GetContentRegionAvail().x`). On whitespace, record a wrap-break opportunity. When the next character would exceed the wrap width, break to a new line (advance Y by line height, reset X)
- **Per-character rendering**: For each character, compute base screen position from `ImGui::GetCursorScreenPos()` + accumulated offset, then:
  1. Collect all active effects from the stack
  2. Compute composite transform: position offset (shake), color modulation (pulse, rainbow, glow), overlay flag (fire, sparkle)
  3. Call `ImDrawList::AddText(font, fontSize, transformedPos, compositeColor, charUtf8, charUtf8 + charLen)` for the glyph
- **Advance cursor**: After rendering all characters, set `ImGui::SetCursorScreenPos()` to the final position (or use `ImGui::Dummy()`) so subsequent ImGui widgets flow correctly

### 3. Implement Each Effect

All effects use `ImGui::GetTime()` for phase and `ImGui::GetIO().DeltaTime` for stepping. Each character's index in the span provides phase offset for wave-like effects.

**a. Pulse** (`<pulse cycle=500ms>`)
- Modulate alpha: `alpha = 0.5 + 0.5 * sin(2π * time / cycleSec)`
- Optionally modulate scale slightly (±5%) for a breathing feel
- Default cycle: 1.0s

**b. Glow** (`<glow color=#ff0 radius=3>`)
- Render the character multiple times at small offsets (up/down/left/right by `radius` pixels) with the glow color at reduced alpha (~0.3)
- Then render the character normally on top
- Default color: gold (#FFD700), default radius: 2px

**c. Shake** (`<shake intensity=2>`)
- Per character, per frame: offset X by `intensity * sin(time * 37.0 + charIndex * 7.3)` and Y by `intensity * cos(time * 29.0 + charIndex * 11.1)` — pseudo-random but deterministic per character to avoid flicker
- Default intensity: 2.0px

**d. Fire Particles** (`<particle effect=fire lifetime=1s>`)
- Maintain a persistent `ParticlePool` (see step 4)
- Each frame, spawn N particles (proportional to text width) at random positions along the text baseline
- Particles drift upward with slight horizontal wander, color transitioning yellow → orange → red → transparent over lifetime
- Render particles as small filled circles via `ImDrawList::AddCircleFilled()`
- Also tint the text itself with a warm gradient (optional)
- Default lifetime: 1.0s

**e. Sparkle** (`<sparkle density=3>`)
- Similar particle pool but particles appear at random positions within the text bounding box
- Short lifetime (0.2–0.5s), appear as small bright dots that fade in/out
- Render as small `AddCircleFilled()` with white/bright color
- Default density: 3 particles per character per second

**f. Rainbow** (`<rainbow speed=1>`)
- Per character, compute hue: `hue = fmod((charIndex * 0.1 + time * speed), 1.0)`
- Convert HSV→RGB using `ImGui::ColorConvertHSVtoRGB()`
- Apply as the text color for that character
- Default speed: 1.0

### 4. Persistent Particle State

Add a `static` particle manager outside `MD4CRenderer` (since the renderer is recreated each frame):

```
struct Particle { ImVec2 pos; ImVec2 vel; float life; float maxLife; ImVec4 color; };

struct ParticlePool {
    std::vector<Particle> particles;
    float lastSpawnTime;
    ImVec2 spawnMin, spawnMax;  // bounding box for spawning
};

static std::unordered_map<uint64_t, ParticlePool> s_particlePools;
```

Key by a hash of `(src_pos, embedCounter or effect index)` to uniquely identify each effect instance. Each frame:
1. Step existing particles (advance position, reduce life, remove dead)
2. Spawn new particles based on spawn rate and DeltaTime
3. Render surviving particles

Prune pools that haven't been touched in 2+ seconds (effect no longer visible).

### 5. Additional `renderText()` Integration

Modify `renderText()` in MarkdownText.cpp:
- At the top, after the list-item and code-block handling, check `if (!effects.empty())` → call `renderTextWithEffects(s)` and return
- Keep all existing logic (bold/italic font stack, link coloring, definition-list heuristic) intact for the non-effect path
- In the effect path, still apply bold/italic fonts from the `spans` stack by selecting the correct `ImFont*` before per-character drawing
- Link coloring (`linkUrl`) also composes: the link blue becomes the base color that effects further modulate

### 6. Table Cell Handling

When `in_table_cell == true`, text is accumulated into `table_cell_text` strings. For effects inside table cells:
- Accumulate effect open/close markers as sentinel sequences in the cell text string
- When the table is rendered in `leave_block(MD_BLOCK_TABLE)`, parse sentinels and render per-character effects within each cell
- **Alternative (simpler, recommended for v1)**: Strip effect tags inside table cells and render plain text. Add a comment noting this limitation.

### 7. Header File Update

Add to MarkdownText.hpp:
- No public API changes needed — all effect code is internal to the renderer
- Optionally add a `void MarkdownTextSetEffectsEnabled(bool)` toggle if you want a global kill switch

### 8. Code Block Passthrough

No changes needed — md4c treats HTML inside code blocks/fences as literal text (`MD_TEXT_CODE`), so effect tags in code won't trigger the parser.

---

**Verification**

Per the project's run-first policy:
1. Build: `cmake --build build-debug --target LoreBook -j 8`
2. Run LoreBook, open or create a vault
3. Create a node with markdown content containing each effect tag:
   ```
   Normal text then <pulse cycle=500ms>pulsing text</pulse> then normal.
   
   <glow color=#FFD700 radius=3>Glowing golden text</glow>
   
   <shake intensity=3>Shaky text here</shake>
   
   <particle effect=fire lifetime=1.5s>This text is on fire</particle>
   
   <sparkle>Sparkly text</sparkle>
   
   <rainbow speed=2>Rainbow cycling text</rainbow>
   
   <glow><shake intensity=1>Composed: glowing + shaking</shake></glow>
   
   <effect type=pulse cycle=200ms>Generic tag syntax</effect>
   ```
4. Verify each effect animates correctly in the node preview
5. Verify composited effects (nested tags) render both effects
6. Verify text wrapping works correctly with effects (resize the preview pane)
7. Verify no effects leak into code blocks: `` `<pulse>literal</pulse>` ``
8. Verify performance stays acceptable with multiple effect spans on screen

**Decisions**
- **Per-character rendering via `ImDrawList::AddText()`** over wrapping `TextWrapped()` — required for per-character transforms but means reimplementing word-wrap logic
- **`static` map for particle state** over embedding in `MD4CRenderer` — renderer is per-frame ephemeral, particles must persist
- **Deterministic pseudo-random shake** (sine with per-char frequency offset) over true `rand()` — avoids jittery flicker, looks smoother
- **Glow via multi-pass offset text rendering** over GL blur shader — simpler, no shader dependency, adequate visual quality
- **Strip effects in table cells for v1** — avoids complex sentinel parsing in deferred table rendering; can be enhanced later