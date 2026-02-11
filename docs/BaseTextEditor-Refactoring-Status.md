# BaseTextEditor Refactoring Status

**Date:** February 11, 2026  
**Goal:** Extract common editor logic from LuaEditor (the most fully-implemented editor) into a `BaseTextEditor` base class, then refactor LuaEditor, JavaEditor, and MarkdownEditor to inherit from it. This fixes MarkdownEditor rendering issues (missing `renderFontSize`, `baselineOffset`, `editorScaleMultiplier`, horizontal scroll, multi-cursor support).

---

## What's Been Done ✅

### 1. BaseTextEditor Header (`include/Editors/Common/BaseTextEditor.hpp`)
- **Created** the `Editors::BaseTextEditor` abstract base class
- **Moved** common structs into `Editors` namespace: `EditorState`, `CompletionItem`, `ExtraCursor`, `UndoEntry`, `EditorTab`
- Contains all shared members:
  - Font metrics: `renderFontSize`, `editorScaleMultiplier`, `charWidth`, `lineHeight`
  - Horizontal scroll: `scrollX`, `visibleWidth`
  - Colors (text, background, syntax highlighting)
  - Multi-cursor support (`extraCursors`)
  - Undo/redo stacks
  - Tab management
- Declares pure virtual methods for language-specific behavior:
  - `drawTextContentWithSyntaxHighlighting()`
  - `updateCompletions()`
  - `updateSyntaxErrors()`
  - `generateContextAwareCompletions()`
- Declares optional overrides: `onTextChanged()`, `loadTabContent()`, `saveActiveTab()`, `getNewFileName()`, `getTabBarId()`, `drawEditorOverlay()`

### 2. BaseTextEditor Implementation (`src/Editors/Common/BaseTextEditor.cpp`)
- **Created** with full implementation ported from LuaEditor (ground truth), including:
  - `updateFontMetrics()` — uses `renderFontSize = font->FontSize * editorScaleMultiplier`
  - `getBaselineOffset()` — `(lineHeight - renderFontSize) * 0.5f`
  - `drawEditor()` — main editor rendering loop with gutter, text area, scrolling
  - `drawLineNumbers()` — with proper `baselineOffset` and `renderFontSize`
  - `drawCursor()` + `drawSelection()` — with `baselineOffset` applied to Y coords
  - `drawSyntaxErrors()` — wavy underline matching LuaEditor
  - `drawCompletionPopup()` — word-replacement behavior matching LuaEditor
  - `handleMouseInput()` — midpoint-based column detection, Ctrl+Alt multi-cursor, Ctrl+wheel zoom, Shift+wheel horizontal scroll
  - `handleKeyboardInput()` — full keyboard handling: clipboard (Ctrl+C/X/V/A), undo/redo (Ctrl+Z/Y), Enter with auto-indent, Backspace/Delete with multi-cursor, arrow keys with Shift+selection, Home/End, Tab with multi-cursor, completion accept
  - `insertTextAtCursor()`, `deleteSelection()`, `moveCursor()`, `pushUndo()`
  - Tab management: `openTab()`, `closeTab()`, `setActiveTab()`, `syncActiveTabToEditor()`, `syncEditorToActiveTab()`
  - Content access: `setContent()`, `getEditorContent()`
  - Cursor positioning: `getCursorScreenPos()`, `ensureCursorVisible()`, `isCursorVisible()`, `scrollToCursorIfNeeded()`

### 3. Updated Header Files (All Three Editors)
- **LuaEditor.hpp** — Now `class LuaEditor : public Editors::BaseTextEditor`
  - Removed duplicate struct definitions (EditorState, CompletionItem, ExtraCursor, UndoEntry, EditorTab)
  - Removed declarations for methods now in base class
  - Kept Lua-specific: ProgramStructure, live console, API docs, preview engine, Lua engine per-tab
  - Overrides: `drawTextContentWithSyntaxHighlighting`, `updateCompletions`, `updateSyntaxErrors`, `generateContextAwareCompletions`, `onTextChanged`, `loadTabContent`, `saveActiveTab`, `drawEditorOverlay`

- **JavaEditor.hpp** — Now `class JavaEditor : public Editors::BaseTextEditor`
  - Removed duplicate struct definitions
  - Removed declarations for methods now in base class
  - Kept Java-specific: ProgramStructure, JVM integration, class investigator, live console
  - Overrides: `drawTextContentWithSyntaxHighlighting`, `updateCompletions`, `updateSyntaxErrors`, `generateContextAwareCompletions`, `onTextChanged`

- **MarkdownEditor.hpp** — Now `class MarkdownEditor : public Editors::BaseTextEditor`
  - Removed duplicate struct definitions
  - Removed declarations for methods now in base class
  - Kept Markdown-specific: MarkdownPreview (2.5D FBO), inline markdown renderer
  - Overrides: `drawTextContentWithSyntaxHighlighting`, `updateCompletions`, `updateSyntaxErrors`, `generateContextAwareCompletions`, `onTextChanged`

### 4. CMakeLists.txt
- **No changes needed** — Uses `file(GLOB_RECURSE ...)` which auto-discovers `src/Editors/Common/BaseTextEditor.cpp`

---

## What's Left To Do ❌

### 1. Refactor `src/Editors/Lua/LuaEditor.cpp` (~2500 lines)
**The .cpp still has all the old code.** Need to:
- **Remove** all methods now in BaseTextEditor (~1300 lines):
  - `updateFontMetrics()`, `drawLineNumbers()`, `drawTextContent()`, `drawCursor()`, `drawSelection()`, `drawSyntaxErrors()`, `drawCompletionPopup()`, `getCursorScreenPos()`, `ensureCursorVisible()`, `isCursorVisible()`, `scrollToCursorIfNeeded()`
  - `insertTextAtCursor()`, `deleteSelection()`, `moveCursor()`, `pushUndo()`
  - `handleKeyboardInput()` (the entire ~300 line method)
  - `handleMouseInput()` (embedded in `drawEditor()`)
  - Tab management: `findTabByPath()`, `openTab()`, `closeTab()`, `setActiveTab()`, `getActiveTab()`, `loadTabContent()`, `syncActiveTabToEditor()`, `syncEditorToActiveTab()`, `saveActiveTab()`
- **Update** `drawEditor()` to call `BaseTextEditor::drawEditor()` instead of duplicating the rendering loop
- **Update** `draw()` to use `BaseTextEditor::drawEditor()` inside tab items
- **Update** `openFile()` to call `BaseTextEditor::openTab()` 
- **Keep** all Lua-specific methods: `drawTextContentWithSyntaxHighlighting()`, `updateCompletions()`, `updateSyntaxErrors()`, `generateContextAwareCompletions()`, `executeLuaCode()`, `drawLiveConsole()`, `drawClassInvestigator()`, preview methods, etc.
- **Update** struct references: `Lua::EditorState` → `Editors::EditorState`, `Lua::EditorTab` → `Editors::EditorTab`, etc.
- **Override** `loadTabContent()` to use `FileBackend` (vault:// URI support)
- **Override** `saveActiveTab()` to use `FileBackend`
- **Override** `drawEditorOverlay()` for the "API Docs" button

### 2. Refactor `src/Editors/Java/JavaEditor.cpp` (~1800 lines)
**Same situation as LuaEditor.** Need to:
- **Remove** all methods now in BaseTextEditor (~1000 lines)
- **Update** `drawEditor()` to call `BaseTextEditor::drawEditor()`
- **Update** `draw()` to use `BaseTextEditor::drawEditor()` inside tab items
- **Keep** Java-specific: `drawTextContentWithSyntaxHighlighting()`, `updateCompletions()`, `updateSyntaxErrors()`, `generateContextAwareCompletions()`, `executeJavaCode()`, `drawLiveConsole()`, `drawClassInvestigator()`, JVM integration
- **Update** struct references to use `Editors::` namespace

### 3. Refactor `src/Editors/Markdown/MarkdownEditor.cpp` (~2100 lines)
**Same situation.** Need to:
- **Remove** all methods now in BaseTextEditor (~1200 lines)
- **Update** `drawEditor()` to call `BaseTextEditor::drawEditor()`
- **Update** `draw()` to use `BaseTextEditor::drawEditor()` inside tab items
- **Keep** Markdown-specific: `drawTextContentWithSyntaxHighlighting()`, `renderInlineMarkdown()`, `updateCompletions()`, `updateSyntaxErrors()`, `generateContextAwareCompletions()`, `drawPreview()`, `syncPreviewSource()`
- **Remove** `setContent()` and `getEditorContent()` (now in base class)
- **Update** struct references to use `Editors::` namespace

### 4. Update External References
Files that reference the old `Lua::EditorState`, `Lua::EditorTab`, `::EditorState`, `::EditorTab`, or `::CompletionItem` types:
- `src/ScriptEditor.cpp` — Uses `Lua::LuaEditor::get()` (should still work, LuaEditor is still in `Lua::` namespace)
- Any code in `src/LoreBook.cpp` that creates/uses MarkdownEditor or JavaEditor instances
- Check for any references to old global-scope `EditorState`/`EditorTab`/`CompletionItem` structs (previously in MarkdownEditor.hpp and JavaEditor.hpp)

### 5. Build and Manual Verification
- Run `cmake --build build-debug --target LoreBook -j 8`
- Fix any compilation errors from type mismatches or missing methods  
- **Test all three editors manually:**
  - LuaEditor: Open a .lua file, verify text rendering alignment, selection, cursor, multi-cursor, undo/redo, completions, live console, API docs
  - JavaEditor: Open a .java file, verify same behaviors
  - MarkdownEditor: Open markdown content in vault, verify text rendering is **no longer broken** (proper baseline offset, correct line spacing, horizontal scroll works)
- Verify Ctrl+wheel zoom works on all three
- Verify Shift+wheel horizontal scroll works on all three

---

## Key Technical Details

### Root Cause of MarkdownEditor Rendering Issues
| Feature | LuaEditor (Working) | MarkdownEditor (Broken) |
|---|---|---|
| Font size | `renderFontSize = font->FontSize * editorScaleMultiplier` | `font->FontSize` directly |
| Baseline offset | `(lineHeight - renderFontSize) * 0.5f` applied to all Y coords | **Missing entirely** |
| Line height | `renderFontSize + ItemSpacing.y * 0.2f` | `font->FontSize + ItemSpacing.y * 0.5f` (too much spacing) |
| Horizontal scroll | Full `scrollX` support with Shift+wheel | **Missing** |
| Multi-cursor | Ctrl+Alt+Click, Ctrl+Alt+Up/Down | **Missing** |
| Editor zoom | Ctrl+wheel changes `editorScaleMultiplier` | **Missing** |

### Architecture After Refactoring
```
Editors::BaseTextEditor (abstract)
├── Font metrics (renderFontSize, baselineOffset, editorScaleMultiplier)
├── Rendering (line numbers, cursor, selection, errors, completion popup)
├── Input (mouse with midpoint column detection, keyboard with multi-cursor)
├── Tab management
├── Undo/redo
├── Content access (setContent/getEditorContent)
│
├── Lua::LuaEditor
│   ├── Lua syntax highlighting
│   ├── Lua completions (ProgramStructure-based)
│   ├── Bracket/keyword matching
│   ├── FileBackend for vault:// URIs
│   ├── Live console + LuaEngine per tab
│   ├── API docs viewer
│   └── Canvas/UI preview
│
├── JavaEditor
│   ├── Java syntax highlighting
│   ├── Java completions (ProgramStructure + JVM reflection)
│   ├── Bracket/keyword matching
│   ├── Live console + JVM REPL
│   └── Class investigator
│
└── MarkdownEditor
    ├── Markdown syntax highlighting (headings, lists, code blocks, inline formatting)
    ├── Effect tag completions (<fire>, <glow>, etc.)
    ├── Line-start completions (##, -, ```, etc.)
    ├── Effect tag matching/validation
    └── 2.5D FBO preview renderer (MarkdownPreview)
```

---

## File Inventory

| File | Status | Action |
|---|---|---|
| `include/Editors/Common/BaseTextEditor.hpp` | ✅ Done | Created |
| `src/Editors/Common/BaseTextEditor.cpp` | ✅ Done | Created (~750 lines) |
| `include/Editors/Lua/LuaEditor.hpp` | ✅ Done | Updated to inherit |
| `src/Editors/Lua/LuaEditor.cpp` | ❌ Not done | Remove ~1300 lines of duplicates, update ~200 lines |
| `include/Editors/Java/JavaEditor.hpp` | ✅ Done | Updated to inherit |
| `src/Editors/Java/JavaEditor.cpp` | ❌ Not done | Remove ~1000 lines of duplicates, update ~150 lines |
| `include/Editors/Markdown/MarkdownEditor.hpp` | ✅ Done | Updated to inherit |
| `src/Editors/Markdown/MarkdownEditor.cpp` | ❌ Not done | Remove ~1200 lines of duplicates, update ~100 lines |
| `CMakeLists.txt` | ✅ OK | Auto-discovers new .cpp via GLOB_RECURSE |
| `src/ScriptEditor.cpp` | ⚠️ Check | May need type reference updates |
