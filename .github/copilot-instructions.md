# Copilot Instructions for LoreBook ✅

Quick purpose: Help an AI agent be immediately productive by highlighting the app architecture, developer workflows, project-specific patterns, and integration points you must know before making changes.

---

## Big-picture architecture 🔧
- Single desktop executable `LoreBook` (entry: `src/LoreBook.cpp`) using ImGui + OpenGL.
- Primary responsibilities:
  - UI & app loop: `src/LoreBook.cpp` — docking layout, menus, modal/popups.
  - Data model / persistence: `include/Vault.hpp` / `src/VaultFactory.cpp` — SQLite-backed vaults, `Vault::createExampleStructure` helper.
  - Assistant / retrieval (RAG): `src/VaultAssistant.cpp` — builds RAG context, uses FTS5, produces JSON outputs for auto-created nodes.
  - OpenAI-compatible client: `include/OpenAIClient.hpp` / `src/OpenAIClient.cpp` — streaming SSE support, custom base URL and headers.
  - UI chat glue: `src/VaultChat.cpp` — endpoint config, model selection, async streaming, buffering (`streamPartials` + mutex).
  - Rendering helpers: `src/GraphView.cpp`, `include/MarkdownText.hpp` (previewing nodes).

## Development & run workflows ⚙️
- Build system: CMake (see `CMakeLists.txt`) with a `vcpkg.json` manifest for deps.
- Quick commands:
  - Configure: `cmake -S . -B build-debug` (prefer `CMakePresets.json`).
  - Build: `cmake --build build-debug --target LoreBook -j 8`.
  - Run: `./bin/LoreBook` (binary produced in `bin/`).
- Debugging (primary verification method): Run the `LoreBook` target in GDB/LLDB (VSCode `cppdbg` configurations or `gdb --args ./bin/LoreBook`).
- Important policy: This project follows a "Run-First / Debugger-Driven" workflow (see `RUN FIRST DEV GUIDE.md`). **Do not add automated tests; run the app and debug changes manually.**

## Integration points & runtime behavior 🧩
- OpenAI integration:
  - API key: `OPENAI_API_KEY` env var is used; UI allows entering a key as override.
  - Custom endpoints & extra headers supported (use `OpenAIClient::setBaseUrl` and `setDefaultHeaders`).
  - Streaming chat: `OpenAIClient::streamChatCompletions` → called from worker thread; `src/VaultChat.cpp` shows how chunks are accumulated and displayed.
  - Use the in-app **Test Endpoint** button to verify header/endpoint changes.
- DB and schema notes:
  - Vaults are SQLite files with tables like `VaultItems`, `VaultItemChildren`, `VaultNodeFilters`.
  - Tags: comma-separated strings (see `include/Vault.hpp` helpers `parseTags`/`joinTags`).
  - FTS5 is used for search; index management occurs in `VaultAssistant::ensureFTSIndex`.
  - Schema migrations are manual — add migration logic when altering the schema (example: the `Vault` constructor patches missing `IsRoot` column).

## Project-specific conventions & patterns 📐
- Immediate-mode UI (ImGui): keep widget-local persistent UI state in `static` locals near the rendering code (see `src/LoreBook.cpp`).
- Database code style: prefer prepared `sqlite3` statements and explicit `sqlite3_finalize` (pattern: `sqlite3_prepare_v2` → `sqlite3_bind_*` → `sqlite3_step` → `sqlite3_finalize`).
- Concurrency: network / model calls run on background threads (`std::async`) and the UI polls futures — **never block the UI thread**.
- Model outputs: assistant endpoints are expected to return plain JSON for auto-node creation; the code strips code fences but prefer asking models for raw JSON (see `VaultAssistant::askJSONWithRAG`).
- Streaming handling: process SSE chunks incrementally and append to a thread-safe buffer (see `streamPartials` and the associated mutex in `src/VaultChat.cpp`).

## Where to look for examples & change points 🔎
- UI patterns and entry: `src/LoreBook.cpp`, `src/VaultChat.cpp`.
- Assistant & RAG: `src/VaultAssistant.cpp` (functions: `retrieveRelevantNodes`, `buildRAGContext`, `askTextWithRAG`, `askJSONWithRAG`).
- Network/OpenAI client: `include/OpenAIClient.hpp`, `src/OpenAIClient.cpp`.
- DB helper & schema: `include/Vault.hpp`, `src/VaultFactory.cpp`, `src/SQLiteBackend.cpp`.
- Resources embedding: `LoreBook_Resources/` and `generated/LoreBook/`.

## Quick actionable rules for agents 📝
- **Always ask the developer to run and manually verify your changes.** Request they run the app, exercise the updated behavior, and confirm results (step through with the debugger if needed). Do not finalize, commit, or push changes until the developer confirms the manual verification.
- Follow the **Run-First** policy: make changes, run the app, and step through with a debugger. No automated tests are added to this repo.
- When changing DB schema, include explicit migration steps and test by creating an example vault (`Vault::createExampleStructure`).
- When altering streaming or threading behavior, update the incremental buffer and ensure mutex safety (see `src/VaultChat.cpp`).
- When adding UI, keep state as `static` locals next to widgets for consistency with the codebase.
- In PRs, include concise manual verification steps (commands to run and expected results) and ask the developer to confirm verification.
- Prefer asking models for raw JSON outputs during assistant integrations — `VaultAssistant` expects that format.

