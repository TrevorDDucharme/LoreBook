# Media Playback & Inline Plots — Design Plan ✅

**Goal:** Add robust inline media and plotting support to LoreBook:
- **Audio:** inline playback for attachments/URLs using FFmpeg for decoding + RtAudio for output
- **Video:** inline playback using FFmpeg for decoding and OpenGL texture upload for display
- **Plots:** inline 2D/3D plotting using ImPlot and ImPlot3D (or ImPlot + lightweight GL-based 3D renderer)

---

## High-level requirements 🎯
- Inline rendering in Markdown flow (similar UX to inline images/models): `![alt](vault://attachment/123)` or special shorthand for plots
- Support local attachments and remote URLs (cached via existing Attachments + ExternalPath model)
- Non-blocking: decoding and network I/O on background threads; main/UI thread performs GL uploads and UI events
- Minimal external API for other code (e.g., `Media::Play(src)`, `VideoWidget::render(src, size)`, `Plot::render(...)`)
- Cross-platform compatibility (Linux, macOS, Windows)

---

## Dependencies & rationale 📦
- FFmpeg (libavformat/libavcodec/libswresample/libswscale) — universal decoder for audio & video formats
- RtAudio — low-latency cross-platform audio output (use miniaudio as fallback if desired)
- ImPlot (and optionally ImPlot3D) — plotting APIs integrated with ImGui
- Existing: stb_image, assimp, OpenGL (already in repo)

Tradeoffs (quick):
| Option | Pros | Cons |
|---|---:|---|
| RtAudio | Low latency, stable cross-platform | External dependency, callback model complexity
| miniaudio | Single-file, very easy to embed | Slightly larger code surface, still very viable
| FFmpeg | Robust decoding (audio/video/subtitles) | Heavy dependency, licensing considerations (LGPL/GPL choices)

---

## Design details — Audio 🔊
1. Decoding pipeline
   - Use FFmpeg to decode source (file, buffer, URL-blob) into interleaved PCM frames (float or int16).
   - Run decoder on a background thread that feeds a lock-free ring buffer / circular buffer.
2. Output pipeline
   - RtAudio callback (or miniaudio) consumes PCM from ring buffer and sends to hardware.
   - On underrun, play silence and mark position; provide statistics for UI.
3. Controls & UI
   - Inline audio player widget (play/pause, seek scrubber, volume, loop, waveform thumbnail option).
   - Hook into `Vault` attachment preview + Markdown renderer so `![audio](vault://attachment/123)` or `[audio](url)` shows player.
4. Attachment integration
   - If `Attachments` contains metadata with `Data` blob, decode from memory buffer; otherwise, rely on existing async fetch to populate the DB, then decode when available.
5. Sync & Seek
   - Decoder supports seek requests (flush decoder, reset ring buffer), UI exposes seek controls.
6. Edge cases
   - Handle sample-rate conversion via `libswresample`.
   - Support multi-channel and stereo downmixing.

---

## Design details — Video ▶️
1. Decoding pipeline
   - FFmpeg demuxer + decoder -> raw frames (YUV or RGB conversion via libswscale)
   - Decode in a background worker that produces frames into a small frame queue (ring buffer). Prefer limiting queue to N frames (e.g., 5) to bound memory.
2. Rendering pipeline
   - Main/UI thread consumes frames: upload to an OpenGL texture (use glTexSubImage2D or PBO for efficiency) and render using FBO/ImGui::Image (same inline pattern as ModelViewer FBO)
3. AV sync
   - Use wall-clock + audio PTS as master (audio output from RtAudio will be used if present). If audio not present, use video clock.
   - Use simple clock correction (drop frames or repeat frames to maintain sync within a small threshold).
4. Controls & UI
   - Inline video widget with controls overlay: play/pause, seek, playback rate, mute, fullscreen / open full viewer.
   - Option to show/hide subtitles (rendered as text overlays) and frame high-resolution export (PNG).
5. Attachment integration
   - Reuse Vault's caching: playback from `Data` blob when available; otherwise create placeholder attachment and call `asyncFetchAndStoreAttachment`.

---

## Design details — 2D / 3D plots 📈🧭
1. 2D plots (ImPlot)
   - API: `Plot2D plot = Plot2D::fromData(name, x[], y[])` and `plot.render(size)`
   - Support common plot types: lines, scatter, heatmap, histogram
   - Allow exporting to PNG and CSV
   - Add interactive controls (zoom, pan, crosshair, selection brushes)
2. 3D plots (ImPlot3D or GL fallback)
   - For simple 3D surfaces and meshes, use ImPlot3D (if available) or reuse `ModelViewer` to render a mesh built from data points
   - API: `Plot3D::surface(zGrid, dims)` or `Plot3D::scatter(points)`
   - Interaction: orbit, zoom, axis labels
3. Inline usage
   - Special markdown syntax or `![plot](vault://plot/123)` to embed generated plots inline
   - Allow saving plot definitions as attachments (JSON + optional cached PNG) so notes can reference reproducible plots

---

## Implementation plan & milestones (suggested) 🛠️
1. Core infra (2–3 days)
   - Add `MediaPlayer` core (FFmpeg helpers for demux/decode, a Frame struct, packet/decoder worker)
   - Add audio ring buffer utilities and safe decode-to-buffer APIs
2. Inline audio widget (1–2 days)
   - Implement RtAudio output wrapper and the audio inline control UI
   - Integrate with `Vault` attachments and Markdown renderer
3. Inline video widget (2–4 days)
   - Implement frame queue -> GL texture upload + inline display component; AV sync with audio
   - Add UI controls & attach to Markdown linker
4. Plot widgets (1–3 days)
   - Add 2D (ImPlot) integration and plot attachment creation API
   - Add 3D plot via ImPlot3D or `ModelViewer`-based mesh rendering and inline embedding
5. Polish & tests (2–3 days)
   - Unit tests for decoding, seek, ring buffer
   - e2e tests: sample audio & video attachments and plot exports
   - Performance tuning and memory limits, progress UI for downloads

---

## Threading & safety notes 🔒
- Decoding must run on background threads and never call into ImGui or OpenGL directly.
- Use a small, bounded frame/packet queue and a lock-free or mutex-protected ring buffer for audio data.
- Ensure GL texture uploads run on the UI thread; use double-buffering (two textures) or PBOs for minimal stalls.
- Use `dbMutex` (existing) for DB updates; integrate progress callbacks safely (poll UI thread for changes).

---

## Files & components to add / modify (suggested) 🔧
- New: `include/MediaPlayer.hpp`, `src/MediaPlayer.cpp` (FFmpeg wrapper, demuxer/decoder, worker)
- New: `include/AudioOutput.hpp`, `src/AudioOutput.cpp` (RtAudio wrapper + ring buffer)
- New: `include/VideoWidget.hpp`, `src/VideoWidget.cpp` (frame queue + texture upload + UI)
- New: `include/PlotWidgets.hpp`, `src/PlotWidgets.cpp` (ImPlot & ImPlot3D helper wrappers)
- Modify: `include/Vault.hpp` + `src/MarkdownText.cpp` to render audio/video/plot inline similar to images/models and to create attachments from URLs
- Tests: `tests/media/*` with sample media, audio decoding, video frame decoding, plot generation

---

## Testing & validation ✅
- Add a small set of sample files (wav, mp3, mp4, mkv, glb, data CSV) and automated smoke tests that decode and attempt to render frames or produce audio buffers (headless where possible)
- Validate AV sync by synthetic test (sine tone + single moving frame) and measure drift

---

## Open questions / decisions ❓
- Do we prefer **RtAudio** (external dependency) or **miniaudio** (single file) as primary output lib? (I recommend RtAudio for robustness, miniaudio is simpler to embed.)
- Should we expose hooks for hardware-accelerated decoding (VA-API / NVDEC) as optional?

---

If you want, I can open a PR implementing the first milestone (core MediaPlayer + audio output + a minimal inline audio player in Markdown). Which piece should I start with? ✅
