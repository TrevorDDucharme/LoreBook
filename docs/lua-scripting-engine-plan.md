# Lua Scripting Engine Plan

## Overview

Add a Lua scripting engine that exposes the full Vault API, with scripts self-declaring their type (Canvas/UI) via a `Config()` function. Scripts embedded in markdown via `vault://Scripts/<name>.lua` are detected, loaded, introspected for their type, then rendered as either a 2D canvas or interactive ImGui panel.

**Key Design Decisions:**
- **Per-embed isolation:** Each markdown embed gets its own Lua state (prevents state pollution, enables multiple instances)
- **Script type via introspection:** Scripts declare type via `Config()` return table rather than URL params—more flexible and self-documenting
- **Sandbox:** Remove dangerous Lua globals (`io`, `os`, `loadfile`, `dofile`), expose only Vault API
- **Canvas implementation:** Uses `ImDrawList` directly for best ImGui integration

---

## Implementation Steps

### 1. Core Lua Engine Wrapper

**Files:** `include/LuaEngine.hpp`, `src/LuaEngine.cpp`

`LuaEngine` class wrapping `lua_State*` with RAII:

```cpp
class LuaEngine {
public:
    LuaEngine();
    ~LuaEngine();
    
    bool loadScript(const std::string& code);
    ScriptConfig callConfig();  // Calls Config(), returns script metadata
    void callRender(float dt);  // Calls Render(dt) for canvas scripts
    void callUI();              // Calls UI() for ImGui scripts
    
    lua_State* state() const { return L; }
    const std::string& lastError() const { return errorMsg; }
    
private:
    lua_State* L = nullptr;
    std::string errorMsg;
    void setupSandbox();  // Remove io, os, loadfile, dofile
};

struct ScriptConfig {
    enum class Type { None, Canvas, UI };
    Type type = Type::None;
    int width = 300;
    int height = 200;
    std::string title;
};
```

**Sandbox setup:**
- Remove `io`, `os`, `loadfile`, `dofile`, `load` from globals
- Keep `math`, `string`, `table`, `pairs`, `ipairs`, `type`, `tostring`, `tonumber`, `print` (redirected to log)
- Error capture with `lua_pcall` and inline error display

---

### 2. Vault Lua Bindings

**Files:** `include/LuaVaultBindings.hpp`, `src/LuaVaultBindings.cpp`

Expose `vault` table with comprehensive node/content/tag/attachment access:

#### Node Access
```lua
vault.getNode(id)           -- Returns {id, name, content, tags} or nil
vault.getNodeByName(name)   -- Returns node table or nil
vault.getAllNodes()         -- Returns array of {id, name} pairs
vault.getChildren(id)       -- Returns array of child node tables
vault.getParents(id)        -- Returns array of parent node tables  
vault.getRoot()             -- Returns root node table
```

#### Node Modification
```lua
vault.createNode(name, parentID, content, tags)  -- Returns new node ID
vault.deleteNode(id)                              -- Returns success bool
vault.renameNode(id, newName)                     -- Returns success bool
```

#### Content
```lua
vault.getContent(id)              -- Returns content string
vault.setContent(id, content)     -- Returns success bool
vault.appendContent(id, text)     -- Returns success bool
```

#### Tags
```lua
vault.getTags(id)           -- Returns array of tag strings
vault.addTag(id, tag)       -- Returns success bool
vault.removeTag(id, tag)    -- Returns success bool
vault.findByTag(tag)        -- Returns array of matching node tables
vault.getAllTags()          -- Returns array of all unique tags
```

#### Attachments
```lua
vault.listAttachments(nodeID)         -- Returns array of attachment metadata
vault.getAttachment(attachmentID)     -- Returns attachment metadata table
vault.getAttachmentData(attachmentID) -- Returns binary data as Lua string
vault.addAttachment(nodeID, name, mimeType, data)  -- Returns attachment ID
```

#### Context
```lua
vault.currentNodeID()     -- ID of node containing this script embed
vault.currentUserID()     -- Logged-in user ID
vault.currentUserName()   -- Logged-in user display name
```

**Return format:** Node data as Lua tables:
```lua
{
    id = 123,
    name = "Character: Aldric",
    content = "# King Aldric\n\nRuler of...",
    tags = {"character", "royalty", "protagonist"}
}
```

---

### 3. Canvas Drawing API

**Files:** `include/LuaCanvasBindings.hpp`, `src/LuaCanvasBindings.cpp`

Expose `canvas` table backed by `ImDrawList*`:

#### Basic Shapes
```lua
canvas.rect(x, y, w, h, color, filled, rounding)
canvas.circle(cx, cy, radius, color, filled, segments)
canvas.line(x1, y1, x2, y2, color, thickness)
canvas.triangle(x1, y1, x2, y2, x3, y3, color, filled)
```

#### Text
```lua
canvas.text(x, y, text, color, fontSize)
canvas.textSize(text)  -- Returns {w, h}
```

#### Advanced 2D Graphics
```lua
canvas.bezierCubic(x1, y1, cx1, cy1, cx2, cy2, x2, y2, color, thickness)
canvas.bezierQuad(x1, y1, cx, cy, x2, y2, color, thickness)
canvas.polyline(points, color, closed, thickness)  -- points = {{x,y}, {x,y}, ...}
canvas.polygon(points, color, filled)
```

#### Transforms & Clipping
```lua
canvas.pushClip(x, y, w, h)
canvas.popClip()
canvas.pushTransform(offsetX, offsetY, scale, rotation)  -- Implemented via offset tracking
canvas.popTransform()
```

#### Image Compositing
```lua
local img = canvas.loadImage("vault://Assets/texture.png")  -- Returns handle
canvas.drawImage(img, x, y, w, h, tint)
canvas.drawImageQuad(img, corners, uvs, tint)  -- corners/uvs = {{x,y}, ...}
```

#### State & Input
```lua
canvas.width()              -- Canvas width in pixels
canvas.height()             -- Canvas height in pixels
canvas.mouseX()             -- Mouse X relative to canvas
canvas.mouseY()             -- Mouse Y relative to canvas
canvas.isMouseDown(button)  -- 0=left, 1=right, 2=middle
canvas.isMouseClicked(button)
canvas.isHovered()          -- Is mouse over canvas?
```

**Color format:** Tables `{r, g, b, a}` (0-1 range) or hex integers `0xRRGGBBAA`

---

### 4. ImGui UI Bindings

**Files:** `include/LuaImGuiBindings.hpp`, `src/LuaImGuiBindings.cpp`

Expose `ui` table wrapping common ImGui widgets:

#### Basic Display
```lua
ui.text(str)
ui.textWrapped(str)
ui.textColored(color, str)
ui.separator()
ui.spacing()
ui.sameLine(offsetX, spacing)
ui.newLine()
ui.dummy(w, h)
```

#### Input Widgets
```lua
local clicked = ui.button(label)
local newChecked, changed = ui.checkbox(label, checked)
local newText, changed = ui.inputText(label, text, maxLen)
local newVal, changed = ui.inputInt(label, value)
local newVal, changed = ui.inputFloat(label, value, step, format)
local newVal, changed = ui.slider(label, value, min, max)
local newVal, changed = ui.sliderInt(label, value, min, max)
```

#### Selection
```lua
local newIdx, changed = ui.combo(label, currentIdx, items)  -- items = {"a", "b", "c"}
local newIdx, changed = ui.listBox(label, currentIdx, items, heightItems)
local selected = ui.selectable(label, isSelected, flags)
```

#### Layout
```lua
ui.beginGroup()
ui.endGroup()
ui.indent(pixels)
ui.unindent(pixels)
ui.columns(count, id, border)
ui.nextColumn()
ui.setColumnWidth(idx, width)
```

#### Trees & Collapsing Headers
```lua
local open = ui.treeNode(label)
ui.treePop()
local open = ui.collapsingHeader(label, flags)
```

#### Tables
```lua
local open = ui.beginTable(id, columns, flags)
ui.endTable()
ui.tableNextRow(flags, minHeight)
ui.tableNextColumn()
ui.tableSetColumnIndex(idx)
ui.tableSetupColumn(label, flags, initWidth)
ui.tableHeadersRow()
```

#### Popups & Tooltips
```lua
ui.openPopup(id)
local open = ui.beginPopup(id, flags)
ui.endPopup()
local open = ui.beginPopupModal(name, flags)
ui.closeCurrentPopup()
ui.beginTooltip()
ui.endTooltip()
ui.setTooltip(text)
```

#### Style
```lua
ui.pushColor(colorIdx, color)  -- colorIdx = ImGuiCol_* constant
ui.popColor(count)
ui.pushStyleVar(varIdx, val)   -- varIdx = ImGuiStyleVar_* constant  
ui.popStyleVar(count)
```

#### Utilities
```lua
ui.getContentRegionAvail()  -- Returns {w, h}
ui.getCursorPos()           -- Returns {x, y}
ui.setCursorPos(x, y)
ui.isItemHovered()
ui.isItemClicked(button)
ui.setItemTooltip(text)
```

---

### 5. Script Management & Caching

**Files:** `include/LuaScriptManager.hpp`, `src/LuaScriptManager.cpp`

```cpp
class LuaScriptManager {
public:
    LuaScriptManager(Vault* vault);
    
    // Get or create engine for a specific embed location
    LuaEngine* getOrCreateEngine(
        const std::string& scriptPath,
        const std::string& embedID,  // Unique per markdown embed
        int64_t contextNodeID        // Node containing the embed
    );
    
    // Hot-reload support
    void invalidateScript(const std::string& scriptPath);
    void invalidateAll();
    
    // Cleanup engines that haven't been used recently
    void pruneUnused(float maxAgeSeconds);
    
private:
    Vault* vault;
    struct EngineInstance {
        std::unique_ptr<LuaEngine> engine;
        ScriptConfig config;
        int64_t contextNodeID;
        float lastUsedTime;
    };
    std::unordered_map<std::string, EngineInstance> engines;  // key = scriptPath + embedID
};
```

**Cache key:** `scriptPath + "::" + embedID` where `embedID` is derived from the byte offset in the markdown source (already tracked as `src_pos` in the renderer).

---

### 6. Markdown Renderer Integration

**File:** `src/MarkdownText.cpp` — modify `enter_span` for `MD_SPAN_IMG`

Detection and rendering flow (around line 548):

```cpp
// In enter_span for MD_SPAN_IMG:
if (src.starts_with("vault://Scripts/") && src.ends_with(".lua")) {
    std::string scriptPath = src;
    int urlW = -1, urlH = -1;
    parseSizeSuffix(src, scriptPath, urlW, urlH);  // Handle ::800x600 suffix
    
    Vault* vault = static_cast<Vault*>(r->ctx);
    if (!vault) {
        ImGui::TextColored({1,0,0,1}, "[Script: no vault context]");
        return 0;
    }
    
    // Generate unique embed ID from source position
    std::string embedID = std::to_string(r->src_pos);
    
    // Get script manager and engine
    LuaScriptManager* mgr = vault->getScriptManager();
    LuaEngine* engine = mgr->getOrCreateEngine(scriptPath, embedID, vault->currentNodeID);
    
    if (!engine || !engine->lastError().empty()) {
        ImGui::TextColored({1,0.3f,0.3f,1}, "Script Error: %s", 
            engine ? engine->lastError().c_str() : "failed to load");
        return 0;
    }
    
    ScriptConfig config = engine->callConfig();
    
    // Apply URL size overrides
    int width = (urlW > 0) ? urlW : config.width;
    int height = (urlH > 0) ? urlH : config.height;
    
    // Render based on script type
    ImGui::PushID(embedID.c_str());
    
    if (config.type == ScriptConfig::Type::Canvas) {
        ImGui::BeginChild("canvas", ImVec2(width, height), true);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        
        // Bind canvas API with draw list and position
        LuaCanvasBindings::bind(engine->state(), dl, pos, width, height);
        
        // Calculate dt from last frame
        static float lastTime = ImGui::GetTime();
        float now = ImGui::GetTime();
        float dt = now - lastTime;
        lastTime = now;
        
        engine->callRender(dt);
        ImGui::EndChild();
    }
    else if (config.type == ScriptConfig::Type::UI) {
        ImGui::BeginChild("ui", ImVec2(width, height), true);
        LuaImGuiBindings::bind(engine->state());
        engine->callUI();
        ImGui::EndChild();
    }
    else {
        ImGui::TextColored({1,0.5f,0,1}, "[Script has no Config() or unknown type]");
    }
    
    ImGui::PopID();
    return 0;
}
```

---

### 7. Vault Script Storage Helpers

**Files:** `include/Vault.hpp`, `src/VaultFactory.cpp`

Add to `Vault` class:

```cpp
// Script management
LuaScriptManager* getScriptManager();

// Convenience helpers for script storage
int64_t storeScript(const std::string& name, const std::string& code);
std::string getScript(const std::string& name);
std::vector<std::string> listScripts();
```

**Implementation:**
- `storeScript()` creates attachment with `ExternalPath = "vault://Scripts/<name>"` and `MimeType = "text/x-lua"`
- `getScript()` calls `findAttachmentByExternalPath()` and returns data as string
- `listScripts()` queries attachments where `ExternalPath LIKE 'vault://Scripts/%'`

Note: The Asset uploader now recognizes `.lua` files and places them under `vault://Scripts/` (MIME `text/x-lua`) instead of `vault://Assets/` when uploaded via the Import Asset dialog.

---

### 8. CMakeLists.txt Updates

Lua is already configured (lines 67-70), add new source files:

```cmake
set(LoreBook_SRCS
    # ... existing files ...
    src/LuaEngine.cpp
    src/LuaVaultBindings.cpp
    src/LuaCanvasBindings.cpp
    src/LuaImGuiBindings.cpp
    src/LuaScriptManager.cpp
)
```

---

### 9. Example Scripts

#### magic_circle.lua (Canvas Example)
```lua
function Config()
    return {
        type = "canvas",
        width = 400,
        height = 400,
        title = "Magic Circle"
    }
end

local rotation = 0
local glyphs = {"α", "β", "γ", "δ", "ε", "ζ"}

function Render(dt)
    rotation = rotation + dt * 0.5
    
    local cx, cy = canvas.width() / 2, canvas.height() / 2
    local gold = {0.9, 0.7, 0.2, 1}
    local blue = {0.2, 0.4, 0.9, 0.8}
    
    -- Outer circle
    canvas.circle(cx, cy, 180, gold, false, 64)
    canvas.circle(cx, cy, 175, gold, false, 64)
    
    -- Inner circles
    canvas.circle(cx, cy, 120, blue, false, 48)
    canvas.circle(cx, cy, 60, blue, false, 32)
    
    -- Rotating triangles
    for i = 1, 3 do
        local angle = rotation + (i - 1) * (math.pi * 2 / 3)
        local r = 150
        local points = {}
        for j = 1, 3 do
            local a = angle + (j - 1) * (math.pi * 2 / 3)
            table.insert(points, {cx + math.cos(a) * r, cy + math.sin(a) * r})
        end
        canvas.polygon(points, gold, false)
    end
    
    -- Glyphs around the circle
    for i, glyph in ipairs(glyphs) do
        local angle = rotation * 0.3 + (i - 1) * (math.pi * 2 / #glyphs)
        local r = 145
        local x = cx + math.cos(angle) * r
        local y = cy + math.sin(angle) * r
        canvas.text(x - 8, y - 8, glyph, gold, 16)
    end
    
    -- Center symbol
    canvas.text(cx - 12, cy - 12, "☉", {1, 1, 1, 1}, 24)
    
    -- Interactive: highlight on hover
    if canvas.isHovered() then
        local mx, my = canvas.mouseX(), canvas.mouseY()
        canvas.circle(mx, my, 20, {1, 1, 1, 0.3}, true, 16)
    end
end
```

#### character_sheet.lua (UI Example)
```lua
function Config()
    return {
        type = "ui",
        width = 350,
        height = 0,  -- Auto-height
        title = "Character Sheet"
    }
end

-- Persistent state
local state = {
    expanded = {stats = true, skills = false, inventory = false}
}

function UI()
    local node = vault.getNode(vault.currentNodeID())
    if not node then
        ui.textColored({1, 0.3, 0.3, 1}, "No node context")
        return
    end
    
    ui.text("Character: " .. node.name)
    ui.separator()
    
    -- Tags display
    ui.text("Tags: ")
    ui.sameLine()
    local tags = vault.getTags(node.id)
    ui.textColored({0.5, 0.8, 1, 1}, table.concat(tags, ", "))
    
    ui.spacing()
    
    -- Stats section
    if ui.collapsingHeader("Stats") then
        if ui.beginTable("stats", 2) then
            local stats = {
                {"Strength", 14},
                {"Dexterity", 12},
                {"Constitution", 16},
                {"Intelligence", 10},
                {"Wisdom", 13},
                {"Charisma", 8}
            }
            for _, stat in ipairs(stats) do
                ui.tableNextRow()
                ui.tableNextColumn()
                ui.text(stat[1])
                ui.tableNextColumn()
                ui.text(tostring(stat[2]))
            end
            ui.endTable()
        end
    end
    
    -- Skills section
    if ui.collapsingHeader("Skills") then
        local skills = {"Athletics", "Stealth", "Perception", "Persuasion"}
        for i, skill in ipairs(skills) do
            local checked = i <= 2  -- Example: first two are proficient
            local newChecked, changed = ui.checkbox(skill, checked)
            if changed then
                -- Could save to node content or custom field
            end
        end
    end
    
    -- Children nodes (inventory, relationships, etc.)
    if ui.collapsingHeader("Related Nodes") then
        local children = vault.getChildren(node.id)
        for _, child in ipairs(children) do
            if ui.selectable(child.name, false) then
                -- Could trigger navigation
            end
        end
        if #children == 0 then
            ui.textColored({0.5, 0.5, 0.5, 1}, "(none)")
        end
    end
end
```

---

## Verification Steps

1. **Build:** `cmake --build build-debug --target LoreBook -j8`

2. **Run:** `./bin/LoreBook`

3. **Create test vault and script:**
   - File → New Vault
   - Create a node
   - Add attachment: upload `.lua` file, set external path to `vault://Scripts/test.lua`
   - In node content, add: `![](vault://Scripts/test.lua)`

4. **Verify functionality:**
   - [ ] Canvas scripts render shapes correctly
   - [ ] Shapes animate based on `dt` parameter
   - [ ] Mouse input works (`canvas.isHovered()`, `canvas.mouseX()`, etc.)
   - [ ] UI scripts display ImGui widgets
   - [ ] Widget interactions work (buttons, checkboxes, inputs)
   - [ ] `vault.*` calls return correct data
   - [ ] Per-embed isolation: two embeds of same script have independent state
   - [ ] Errors display inline in red text
   - [ ] Size suffix `::800x600` overrides Config dimensions

5. **Security verification:**
   - [ ] `io.open()` fails (sandboxed)
   - [ ] `os.execute()` fails (sandboxed)
   - [ ] `loadfile()` fails (sandboxed)

---

## File Summary

| File | Purpose |
|------|---------|
| `include/LuaEngine.hpp` | Lua state wrapper, sandbox setup, entry point calling |
| `src/LuaEngine.cpp` | Implementation |
| `include/LuaVaultBindings.hpp` | Vault API exposed to Lua |
| `src/LuaVaultBindings.cpp` | Implementation |
| `include/LuaCanvasBindings.hpp` | 2D drawing API for canvas scripts |
| `src/LuaCanvasBindings.cpp` | Implementation using ImDrawList |
| `include/LuaImGuiBindings.hpp` | ImGui widget bindings for UI scripts |
| `src/LuaImGuiBindings.cpp` | Implementation |
| `include/LuaScriptManager.hpp` | Script caching, per-embed isolation |
| `src/LuaScriptManager.cpp` | Implementation |
| `src/MarkdownText.cpp` | Modified to detect and render `vault://Scripts/` embeds |
| `include/Vault.hpp` | Added script storage helpers |
| `CMakeLists.txt` | Add new source files |

---

## Dependencies

Already configured in project:
- **Lua** (vcpkg.json): `"lua": { "features": ["tools", "cpp"] }`
- **ImGui**: Provides `ImDrawList` for canvas rendering
- **md4c**: Markdown parsing (callbacks already in place)

No new dependencies required.
