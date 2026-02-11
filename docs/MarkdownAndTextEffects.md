# LoreBook Markdown & Text Effects Reference

This document covers the markdown syntax supported by LoreBook's built-in renderer, the text effects system, and the `vault://` URI scheme for embedding vault resources.

---

## Table of Contents

1. [Markdown Syntax](#markdown-syntax)
   - [Headers](#headers)
   - [Paragraphs](#paragraphs)
   - [Emphasis & Inline Styles](#emphasis--inline-styles)
   - [Links](#links)
   - [Images](#images)
   - [Lists](#lists)
   - [Task Lists (Checkboxes)](#task-lists-checkboxes)
   - [Code](#code)
   - [Tables](#tables)
   - [Blockquotes](#blockquotes)
   - [Horizontal Rules](#horizontal-rules)
   - [Definition Lists](#definition-lists)
2. [Vault URI Scheme](#vault-uri-scheme)
   - [vault://Assets/](#vaultassets)
   - [vault://World/](#vaultworld)
   - [vault://Scripts/](#vaultscripts)
   - [Size Suffix](#size-suffix)
3. [Text Effects](#text-effects)
   - [Basic Syntax](#basic-syntax)
   - [Compose Syntax](#compose-syntax)
   - [Preset Syntax](#preset-syntax)
   - [Procedural Syntax](#procedural-syntax)
   - [Modifier Reference](#modifier-reference)
   - [Parameters](#parameters)
   - [Preset Reference](#preset-reference)
   - [Nesting & Stacking](#nesting--stacking)

---

## Markdown Syntax

LoreBook uses the **md4c** CommonMark parser and renders directly into ImGui widgets. Most standard markdown is supported.

### Headers

```markdown
# Header 1
## Header 2
### Header 3
#### Header 4
##### Header 5
###### Header 6
```

Headers render in **bold** with decreasing font scale (1.4× for H1 down to 0.9× for H6).

### Paragraphs

Plain text separated by blank lines forms paragraphs. Text automatically wraps to the available content width.

```markdown
This is a paragraph.

This is another paragraph.
```

### Emphasis & Inline Styles

```markdown
*italic text*
**bold text**
`inline code`
```

- *Italic* — renders using the italic font variant
- **Bold** — renders using the bold font variant
- `Code` — renders using the monospace font family

### Links

```markdown
[Link text](https://example.com)
```

Links render in blue (`#4287F5`). Clicking follows the URL.

### Images

```markdown
![Alt text](https://example.com/image.png)
![Alt text](vault://Assets/my_image.png)
```

Images are loaded asynchronously and cached. Supported formats include common image types (PNG, JPEG, etc.) and 3D model formats (`.obj`, `.fbx`, `.gltf`, `.glb`, `.ply`, `.dae`, `.stl`).

3D models render as interactive inline viewers. Clicking opens the full model viewer.

### Lists

#### Unordered Lists

```markdown
- Item one
- Item two
  - Nested item
```

Unordered items display with a bullet point icon. Lists indent 1.5× the font size per level.

#### Ordered Lists

```markdown
1. First item
2. Second item
3. Third item
```

Ordered items display their number followed by a period.

### Task Lists (Checkboxes)

```markdown
- [ ] Unchecked task
- [x] Completed task
```

Task list markers `[ ]` and `[x]` render as **interactive checkboxes**. Clicking a checkbox toggles it and updates the source text in the vault item content.

### Code

#### Inline Code

```markdown
Use `printf()` to print.
```

#### Code Blocks

````markdown
```python
def hello():
    print("Hello, world!")
```
````

Code blocks render in a child window with a dark background (`#141414`) and rounded corners. The language hint is parsed but used for display context only (no syntax highlighting).

### Tables

```markdown
| Header 1 | Header 2 | Header 3 |
|----------|----------|----------|
| Cell 1   | Cell 2   | Cell 3   |
| Cell 4   | Cell 5   | Cell 6   |
```

Tables render using ImGui's table system with inner borders and alternating row backgrounds. Header cells are rendered in **bold**.

### Blockquotes

```markdown
> This is a blockquote.
> It can span multiple lines.
```

Blockquotes render with grey text and a left indent.

### Horizontal Rules

```markdown
---
```

Renders as an ImGui separator line.

### Definition Lists

LoreBook supports a heuristic definition-list syntax (not part of CommonMark):

```markdown
Term: This is the definition of the term.
```

The **term** (text before the first colon) renders in bold, and the **definition** (text after the colon) renders as wrapped text on the same line. This heuristic applies only to top-level text (not inside lists, tables, or code blocks) where the colon appears within the first 80 characters.

Leading-colon definitions render indented and muted:

```markdown
: This is an indented definition.
```

---

## Vault URI Scheme

LoreBook defines a custom `vault://` URI scheme for embedding resources stored in the vault database. These URIs work inside standard markdown image syntax.

### vault://Assets/

```markdown
![My Image](vault://Assets/path/to/image.png)
![3D Model](vault://Assets/models/sword.glb)
```

Resolves vault attachments by their external path. Supports:
- **Images** — rendered inline, loaded asynchronously, cached as GPU textures
- **3D Models** (`.obj`, `.fbx`, `.gltf`, `.glb`, `.ply`, `.dae`, `.stl`) — rendered as interactive inline model viewers

### vault://World/

```markdown
![Globe](vault://World/MyWorld[seed=42,octaves=6]/globe)
![Map](vault://World/MyWorld[seed=42]/mercator)
```

Embeds a procedural world map. The path format is:

```
vault://World/<WorldName>[<config>]/<projection>
```

- **WorldName** — identifier for the world
- **config** (in brackets) — world generation parameters (e.g., `seed=42,octaves=6`)
- **projection** — either `globe` (square aspect) or `mercator` (2:1 aspect)

World maps are cached and re-parsed when the config changes.

### vault://Scripts/

```markdown
![Script](vault://Scripts/magic_circle.lua)
```

Embeds a Lua script as an interactive widget. Scripts can be either:
- **Canvas** type — renders via an OpenGL FBO with full mouse/keyboard event support (click, drag, scroll, hover)
- **UI** type — renders ImGui widgets directly

Scripts are managed by the `LuaScriptManager` and support per-embed instances, hot error display, and click-to-open-in-explorer.

### Size Suffix

Any image or embed URL can include an optional size suffix to override display dimensions:

```markdown
![Model](vault://Assets/sword.glb::800x600)
![Image](https://example.com/photo.jpg::640x480)
![Script](vault://Scripts/demo.lua::400x300)
```

Format: `::WIDTHxHEIGHT` appended to the URL. Either width or height can be omitted (e.g., `::800x` for width-only).

---

## Text Effects

LoreBook's text effects system applies animated per-character visual effects to markdown text. Effects are specified using HTML-style tags that the renderer intercepts.

### Basic Syntax

Use a **modifier name** as an HTML tag:

```markdown
<wave>This text waves up and down</wave>
<shake>This text shakes randomly</shake>
<rainbow>This text cycles through colors</rainbow>
<glow color=#FF0000>Red glowing text</glow>
```

The tag name must be a recognized modifier name (case-insensitive). Parameters are specified as `key=value` or `key="value"` attributes.

### Compose Syntax

Use the generic `<effect>` tag with **bare word** modifier names to combine multiple effects:

```markdown
<effect wave bounce glow speed=2 color=#FFD700>Wavy bouncy glowing text</effect>
```

All bare words are parsed as modifier names. Key-value attributes apply to **all** modifiers in the effect.

### Preset Syntax

Use the `preset` attribute to apply a named preset configuration:

```markdown
<effect preset=fire>Burning text</effect>
<effect preset=ice>Frozen text</effect>
<effect preset=magic>Magical text</effect>
```

See [Preset Reference](#preset-reference) for the full list.

### Procedural Syntax

Use the `seed` attribute to generate a unique random effect:

```markdown
<effect seed=42>Procedurally generated effect</effect>
<effect seed=12345>Different random effect</effect>
```

The procedural generator picks a random combination of:
- 1 color modifier (always)
- 1 position modifier (60% chance)
- 1 alpha modifier (40% chance)
- 1 decoration modifier (50% chance)
- 1 particle modifier (30% chance)

The same seed always produces the same effect.

### Modifier Reference

#### Position Modifiers

| Modifier | Tag Names | Description |
|----------|-----------|-------------|
| Shake | `shake` | Random per-character displacement. Higher amplitude = more violent. |
| Wave | `wave` | Smooth sinusoidal vertical oscillation. Characters form a wave pattern. |
| Bounce | `bounce` | Per-character absolute-value sine bounce — characters hop in place. |
| Wobble | `wobble` | Horizontal sinusoidal oscillation — characters sway side to side. |
| Drift | `drift` | Slow uniform positional drift. `phase` sets direction (radians; π/2 = upward). |
| Jitter | `jitter` | High-frequency tiny random displacement — subtle nervous tremor. |
| Spiral | `spiral` | Circular per-character motion — characters orbit their home positions. |

#### Color Modifiers

| Modifier | Tag Names | Description |
|----------|-----------|-------------|
| Rainbow | `rainbow` | Per-character hue cycling through the full spectrum over time. |
| Gradient | `gradient` | Top-to-bottom color blend from `color` to `color2`. |
| ColorWave | `colorwave` | Sinusoidal hue shift that rolls across characters like a wave. |
| Tint | `tint` | Multiplies text color by the specified `color`. |
| Neon | `neon` | Bright saturated color with pulsing brightness for an electric glow. |
| Chromatic | `chromatic` | RGB channel separation — draws red/blue offset copies for a glitch look. |

#### Alpha Modifiers

| Modifier | Tag Names | Description |
|----------|-----------|-------------|
| Pulse | `pulse` | Uniform alpha oscillation — all characters fade in and out together. |
| FadeWave | `fadewave` | Per-character wave fade — a transparency wave rolls through the text. |
| Flicker | `flicker` | Random alpha jitter per character — chaotic blinking effect. |
| Typewriter | `typewriter` | Characters appear sequentially over time, revealing text left-to-right. |

#### Additive / Decoration Modifiers

| Modifier | Tag Names | Description |
|----------|-----------|-------------|
| Glow | `glow` | Multi-ring graduated soft halo around each character. `radius` controls size. |
| Shadow | `shadow` | Dark offset copy beneath each character for a drop-shadow effect. |
| Outline | `outline` | Thin colored outline drawn around each character (8-direction copies). |
| FireEmit | `fireemit`, `fire` (alias) | Rising ember circles that float upward from the text. |
| FairyDust | `fairydust`, `sparkle` (alias) | Twinkling fairy particles that orbit and fade around characters. |
| Snow | `snow` | Falling white particles drifting downward through the text region. |
| Bubbles | `bubbles` | Rising translucent circles floating upward. |
| Drip | `drip`, `blood` (alias) | Drops cling to the bottom of characters, slowly grow, then detach and fall with gravity acceleration. Text is tinted dark red. |

> **Note:** `particle` is an additional alias that defaults to `FireEmit`.

### Parameters

All parameters are optional and have sensible defaults. When specified as tag attributes, they apply to **all** modifiers in the effect.

| Parameter | Aliases | Default | Description |
|-----------|---------|---------|-------------|
| `speed` | — | `1.0` | Animation speed multiplier. Higher = faster animation. |
| `amplitude` | `amp`, `intensity` | `2.0` | Displacement strength in pixels (position modifiers) or effect intensity. |
| `frequency` | `freq` | `1.0` | Spatial frequency — controls per-character phase stepping for waves. |
| `phase` | — | `0.0` | Initial phase offset (radians). For `drift`, sets the movement direction angle. |
| `color` | — | `#FFD700` (gold) | Primary color as a hex code (e.g., `#FF0000`, `#FF0000FF` with alpha). |
| `color2` | — | `#FF0000` (red) | Secondary color for gradients and dual-color effects. |
| `radius` | `rad` | `2.0` | Glow/outline radius in pixels. |
| `density` | — | `3.0` | Particle spawn density (particles per character). |
| `lifetime` | `life` | `1.0` | Particle lifetime in seconds. |
| `cycle` | — | — | Sets speed as `1.0 / cycle`. Accepts time suffixes: `500ms`, `2s`. |
| `effect` | — | — | Override modifier type by name (e.g., `<particle effect=fire>`). |
| `preset` | — | — | Load a named preset instead of building from modifiers. |
| `seed` | — | — | Generate a procedural effect from this integer seed. |

#### Color Format

Colors are specified as hex strings with an optional `#` prefix:

- `#FF0000` — red (RGB)
- `#FF000080` — red at 50% opacity (RGBA)
- `FF0000` — also valid (no `#` prefix)

### Preset Reference

Presets are pre-configured combinations of modifiers. Use `<effect preset=NAME>`.

| Preset | Modifiers | Description |
|--------|-----------|-------------|
| `neon` | Glow (cyan) + Pulse | Bright cyan glow with gentle pulsing. |
| `fire` | Gradient (orange→red) + FireEmit + Wave | Fiery gradient with rising embers and subtle wave motion. |
| `ice` | Tint (cyan) + Snow + Glow (blue) | Icy blue text with falling snow and cool glow. |
| `magic` | Rainbow + FairyDust + Glow (gold) | Iridescent color cycling with sparkle particles and golden glow. |
| `ghost` | Pulse + FadeWave + Drift (up) + Tint (pale blue) | Semi-transparent drifting text that fades in and out. |
| `electric` | Neon (yellow) + Shake + Flicker | Electrified yellow text with violent shaking and flickering. |
| `underwater` | Tint (blue) + Wave + Bubbles | Blue-tinted text with wavy motion and rising bubbles. |
| `golden` | Glow (gold) + FairyDust (gold) | Warm gold glow with golden fairy dust particles. |
| `toxic` | Neon (green) + Bubbles (green) + Shake | Toxic green glow with bubbling and trembling. |
| `shadow` | Shadow + Flicker | Dark shadow effect with intermittent flickering. |
| `crystal` | Rainbow (slow) + Glow + FairyDust | Prismatic color shift with crystalline glow and sparkles. |
| `storm` | Shake (strong) + Flicker (fast) + Chromatic | Violent shaking with chromatic aberration — a visual storm. |
| `ethereal` | Pulse (soft) + Drift (up) + FairyDust (pale) | Gentle floating effect with faint particles — otherworldly calm. |
| `lava` | Gradient (red→yellow) + Glow (orange) + Wave (slow) | Molten lava gradient with orange glow and gentle wave. |
| `frost` | Gradient (white→cyan) + Snow + Glow (light blue) | Frosty white-to-cyan with falling snow and icy glow. |
| `void` | Tint (purple) + Pulse + Jitter | Dark purple tint with pulsing and nervous jitter — the void stares back. |
| `holy` | Glow (gold, large) + FairyDust (gold) + Pulse (gentle) | Radiant gold halo with heavenly sparkles. |
| `matrix` | Typewriter (green) + Neon (green) | Green characters appearing sequentially with neon glow — digital rain. |
| `blood` | Drip (red) + Pulse (subtle) | Dark red text with drops that cling, grow, and fall with gravity. |
| `disco` | Rainbow (fast) + Bounce + Glow | Rapid color cycling with bouncing characters and glow — party mode. |
| `glitch` | Chromatic (strong) + Jitter (fast) + Flicker (fast) | Aggressive RGB separation, rapid jitter, and heavy flicker. |

### Nesting & Stacking

Effects can be nested. Inner effects stack on top of outer effects — all active modifiers are merged and applied together:

```markdown
<glow color=#0000FF>
  This text glows blue.
  <wave>This text glows blue AND waves.</wave>
  Back to just blue glow.
</glow>
```

Effects are cleared automatically at block boundaries (paragraph, header, code block, blockquote), so unclosed tags won't leak between sections.

Closing tags match by name — `</wave>` removes the nearest `wave` from the stack regardless of nesting order.

### Examples

```markdown
<!-- Simple single modifier -->
<shake>Earthquake!</shake>

<!-- Single modifier with parameters -->
<wave speed=3 amplitude=8 color=#00FF00>Green waves</wave>

<!-- Compose multiple modifiers -->
<effect glow rainbow bounce speed=2>Party text!</effect>

<!-- Use a preset -->
<effect preset=fire>The kingdom burns</effect>

<!-- Procedural generation -->
<effect seed=777>Every seed is unique</effect>

<!-- Nested effects -->
<glow color=#FFD700 radius=4>
  The <shake amplitude=3>ancient</shake> scroll glows with power.
</glow>

<!-- Size-suffixed image embed -->
![Dragon](vault://Assets/dragon.png::600x400)

<!-- World map embed -->
![Map](vault://World/Aetheria[seed=42,octaves=8]/mercator)

<!-- Script embed with custom size -->
![Magic Circle](vault://Scripts/magic_circle.lua::500x500)
```
