# Copilot Instructions for LoreBook ✅

Purpose: Quickly onboard AI coding agents with the precise, actionable knowledge they need to be productive in this repository.

## Big-picture architecture 🔧
- Desktop app (ImGui + OpenGL) with a single executable: `LoreBook` (entry point: `src/LoreBook.cpp`).
- Core components:
  - UI host & main loop: `src/LoreBook.cpp` (docking layout, menus, modals)
  - Data model / persistence: `include/Vault.hpp` (SQLite DB schema, tag format, child relations)
  - Search / assistant layer: `src/VaultAssistant.cpp` (RAG, FTS index, schema-based JSON creation)
  - OpenAI-compatible HTTP client: `include/OpenAIClient.hpp` / `src/OpenAIClient.cpp` (supports streaming & custom base URL / headers)
  - UI-facing chat controls: `src/VaultChat.cpp` (endpoint config, model selection, streaming & async usage)
  - Graph & preview: `src/GraphView.cpp`, `include/MarkdownText.hpp` (rendering helpers)

## Build & development workflows ⚙️
- Project uses CMake + vcpkg manifest (`vcpkg.json`). See `CMakeLists.txt` and `CMakePresets.json` for presets.
- Typical build steps (use presets or manual):
  - Configure: `cmake -S . -B build-debug` (prefer CMake Presets)
  - Build: `cmake --build build-debug --target LoreBook -j 8`
  - Binary output: `./bin/LoreBook`
- Debugging: run the `LoreBook` target under your C++ debugger (GDB/LLDB via VSCode `cppdbg` or your IDE). The project already has a `build-debug` folder in workspace.

## Runtime & integration points 🧩
- OpenAI integration:
  - API key: environment variable `OPENAI_API_KEY` is respected; UI also allows entering a key.
  - Endpoint customization + extra headers (for Azure or HF) via the Vault Chat UI (see `src/VaultChat.cpp`). Example header for Azure: `api-key: <key>`.
  - `OpenAIClient` supports `setBaseUrl()` and `setDefaultHeaders()` for non-OpenAI endpoints.
  - Streaming: `OpenAIClient::streamChatCompletions` sets `stream=true` and expects to be run on a worker thread; `src/VaultChat.cpp` demonstrates an async streaming pattern that accumulates chunks into a per-vault buffer.
- DB and schema:
  - Vaults are SQLite files (created by the app). Default/example vault creation happens via the "New Vault" flow (`Vault::createExampleStructure`).
  - Vault schema: `VaultItems`, `VaultItemChildren`, `VaultNodeFilters`. Per-node child filters are persisted (see `saveNodeFilterToDB`/`loadNodeFiltersFromDB`).
  - Tags are stored as comma-separated strings (see `parseTags` / `joinTags` helpers in `include/Vault.hpp`).
  - FTS5 index is used for retrieval (`VaultAssistant::ensureFTSIndex` rebuilds it).

## Project-specific conventions & patterns 📐
- Immediate-mode UI with ImGui — UI state lives next to rendering code (see many `static` UI buffers in `src/LoreBook.cpp` & `include/Vault.hpp`). Follow the same style when adding UI: keep widget-local persistent state in `static` locals near rendering code.
- SQL safety: code frequently uses prepared sqlite3 statements and manually finalizes them — when editing DB code follow existing patterns (`sqlite3_prepare_v2`, `sqlite3_bind_*`, `sqlite3_step`, `sqlite3_finalize`).
- Model outputs and JSON handling: when auto-creating nodes the assistant expects pure JSON conforming to a small schema. Code attempts to strip code fences before parsing; prefer asking model to return raw JSON (see `VaultAssistant::askJSONWithRAG`).
- Concurrency: network calls are run on background threads/futures; UI polls futures for completion (`std::async` patterns in `src/VaultChat.cpp`). Avoid blocking UI thread.

## Where to look for examples & change points 🔎
- Add new UI: follow `src/LoreBook.cpp`, `src/VaultChat.cpp`, `include/Vault.hpp` for modal/popup patterns.
- Add network / OpenAI behavior: `include/OpenAIClient.hpp` + `src/OpenAIClient.cpp` (covers copyable client usage, headers, stream callback semantics).
- Add search/assistant logic or change RAG: `src/VaultAssistant.cpp` (retrieveRelevantNodes, buildRAGContext, askTextWithRAG, askJSONWithRAG).
- Asset and resource embedding: `LoreBook_Resources/` and generated code under `generated/LoreBook/`.

## Quick actionable notes for agents 📝
- When modifying API call behavior, ensure `OPENAI_API_KEY` env var and UI overrides remain supported and test endpoint with the in-app "Test Endpoint" button.
- When changing DB layout, provide migration logic (see `Vault` constructor that adds `IsRoot` column if missing).
- For streaming responses, always process SSE chunks incrementally and update a thread-safe partial buffer (see `streamPartials` + mutex in `src/VaultChat.cpp`).
- Use the existing `createExampleStructure` helper to produce reproducible test data for manual testing.
