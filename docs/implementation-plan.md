# Torrview Research Findings and Implementation Plan

Last updated: 2026-07-03

## Product Goal

Build a native desktop app that lets a user watch video files directly from a torrent source.

Primary user flow:

1. Open app.
2. Drop a `.torrent` file, paste a magnet link, or open a native file picker.
3. Load torrent metadata.
4. Show files in a custom UI.
5. User selects a video file.
6. Player opens and streams the selected file using a bounded rolling buffer.

Core technologies requested:

- `libmpv` for video playback.
- `Clay` for UI layout.
- `libtorrent` for torrent networking.
- Strict no-persistent-download behavior.
- Rolling video buffer around current playback position.
- Default buffer: `128 MiB`, configurable in player.

## Key Product Decisions

### Native Shell

Use C++20 plus Meson.

Use SDL3 for native windowing, events, drag and drop, clipboard, OpenGL context creation, and native file dialogs. SDL3 gives a practical cross-platform base while still producing a native desktop application.

Initial target platforms are Windows and Linux.

Linux packaging should prefer system dependencies. Windows packaging should bundle required runtime libraries. In both cases, statically link as much as practical while staying compatible with dependency licenses and platform conventions.

### UI

Use Clay for layout only. Clay is not a widget toolkit and does not render by itself. The app must provide:

- OpenGL renderer for Clay primitives.
- Font/text shaping path.
- Input routing.
- Focus, hover, active, scroll, and selection state.
- Widgets: buttons, text input, file list, sliders, menus, tabs, overlays.

### Video Playback

Use libmpv render API with OpenGL.

The app owns the OpenGL context and asks mpv to render video into the current framebuffer or a dedicated FBO. UI can be composited above the video with Clay-rendered overlays.

Use a custom mpv/FFmpeg build for packaged builds. FFmpeg should be configured with only the demuxers, parsers, protocols, and video decoders required for the supported media scope. Any audio decoder enablement must be minimal and explicit. Keep this as a packaging/build task, because system libmpv on Linux may use the distribution FFmpeg.

Use mpv custom stream callbacks for torrent-backed media:

- Register a read-only protocol, for example `torrview://<torrent-id>/<file-index>`.
- Implement `open`, `read`, `seek`, `size`, `close`, and `cancel`.
- `read` blocks until verified bytes are available or the stream is cancelled.
- Stream callbacks must not call libmpv APIs.

### Torrent Client

Use libtorrent 2.x.

Important: in libtorrent 2.x, custom storage is session-level `disk_interface`, not the old per-torrent `storage_interface`.

For strict no-disk mode, implement a RAM-backed `disk_interface`. No temporary disk cache mode is planned.

Uploads should be disabled. If libtorrent cannot fully disable all upload behavior for protocol reasons, set the upload limit and upload slots as close to zero as supported and prevent serving evicted pieces.

### Buffer Policy

The player must buffer only a bounded amount around current playback progress.

Default:

- Total cache limit: `128 MiB`.
- Suggested split: `32 MiB` behind playback position and `96 MiB` ahead.
- Actual memory may be `limit + piece_size + in-flight blocks`, because torrent data is piece/block based.

Configurable in player:

- Presets: `64 MiB`, `128 MiB`, `256 MiB`, `512 MiB`.
- Optional custom value.
- UI displays current cache usage, for example `84 MiB / 128 MiB`.

## Major Technical Constraint: Libtorrent Piece Eviction

The hardest part is strict bounded RAM with libtorrent.

Libtorrent exposes APIs for:

- Checking whether a piece is complete: `have_piece()`.
- Reading a complete piece: `read_piece()`.
- Setting piece deadlines: `set_piece_deadline()`.
- Setting file and piece priorities.
- Clearing deadlines.

It does not expose a clean public API to mark a previously completed piece as "not have" again.

That means a RAM store can physically evict old pieces, but libtorrent may still believe those pieces are available. This creates risks:

- Backward seek to an evicted piece may need rebuffer/reopen.
- Peer upload requests for evicted pieces can fail unless uploads are disabled.
- Progress reporting from libtorrent can overstate retained local data.

Implementation must treat this as a first-class risk and verify behavior early.

Recommended strategy:

1. Keep app-level `PieceState` separate from libtorrent's view.
2. Use libtorrent priorities/deadlines to avoid downloading outside the active window.
3. Retain only the configured RAM window in `MemoryDiskIO`.
4. Disable uploads from this playback session.
5. If the user seeks outside the retained window and libtorrent state prevents clean redownload, remove and re-add the torrent handle using retained metadata, then schedule the new window.
6. Do not add a disk-backed fallback. Strict RAM mode is the product direction.

## Architecture

### Modules

`App`

- Owns SDL window, event loop, OpenGL context, and high-level routing.
- Coordinates app lifecycle and shutdown.

`Ui`

- Builds Clay layout tree.
- Owns UI state: active screen, file selection, controls, settings.
- Does not talk directly to libtorrent or libmpv internals.

`ClayRenderer`

- Converts Clay render commands to OpenGL draw calls.
- Handles clipping/scissor, rectangles, borders, text, icons, and overlays.

`MpvPlayer`

- Owns `mpv_handle`.
- Creates and owns `mpv_render_context`.
- Registers custom stream protocol.
- Observes playback properties such as pause state, duration, time position, cache state, tracks, and end-file.
- Sends commands using argument arrays, not shell/string command construction.

`TorrentService`

- Owns `lt::session`.
- Adds torrent files and magnet links.
- Processes alerts.
- Publishes torrent metadata, file lists, peer counts, speeds, errors, and piece completion events to app state.

`MemoryDiskIO`

- Implements libtorrent `disk_interface`.
- Stores blocks/pieces in RAM.
- Verifies v1/v2 hashes.
- Tracks bytes used and piece residency.
- Supports bounded eviction under `CacheManager` policy.

`TorrentStream`

- Implements mpv stream callbacks.
- Maps mpv byte reads/seeks to torrent file offsets.
- Waits for verified piece data.
- Handles cancellation when playback stops or file changes.

`PieceScheduler`

- Converts playback position and read requests into torrent piece priorities and deadlines.
- Maintains rolling window.
- Clears stale deadlines on seek.
- Tells `MemoryDiskIO` which pieces are retained, warm, urgent, or evictable.

`Settings`

- Persists player configuration.
- Includes buffer limit, volume, window state, and optional networking preferences.

### Thread Model

Main thread:

- SDL events.
- Clay layout.
- OpenGL rendering.
- mpv render calls.

mpv demux/read threads:

- Call `TorrentStream` callbacks.
- May block in `read`.
- Must not call libmpv APIs from stream callbacks.

libtorrent session thread:

- Runs networking and torrent state internally.
- Calls `MemoryDiskIO` methods according to libtorrent disk subsystem rules.

App worker/bridge:

- Processes torrent alerts.
- Wakes stream readers when required pieces become available.

Cross-thread rules:

- Use mutexes/condition variables for stream reads.
- Use lock-free or mutex-protected queues for alerts/events.
- Avoid synchronous `torrent_handle` calls on the UI thread when possible.

## Playback and Buffer Algorithm

### File Range Mapping

For selected file:

- Get file offset within torrent.
- Convert file byte offset to torrent byte offset.
- Convert torrent byte range to piece range using torrent piece length.
- Include partial first and last pieces.

### Initial Playback

1. User selects a file.
2. All torrent files get priority `0`.
3. Selected video file gets priority `high`.
4. No subtitle sidecars are auto-loaded for MVP.
5. Compute initial cache window from byte offset `0`.
6. Set urgent deadlines for initial pieces.
7. Open `torrview://...` in mpv.
8. mpv `read` blocks until enough bytes are verified.

### Rolling Window

On playback progress update or stream read:

1. Determine current byte offset.
2. Compute target retained range:
   - Behind range: default `32 MiB`.
   - Ahead range: default `96 MiB`.
3. Mark pieces inside range as retained.
4. Mark near-future pieces as deadline-driven.
5. Reset deadlines outside range.
6. Set priorities outside range to `0` where possible.
7. Evict RAM pieces outside retained range once memory exceeds limit.

### Seek

On seek:

1. Cancel pending blocking reads.
2. Clear piece deadlines.
3. Recompute cache window around target byte offset.
4. If target pieces are retained, serve immediately.
5. If target pieces were evicted but libtorrent can redownload cleanly, schedule them.
6. If libtorrent believes evicted pieces are still complete and will not redownload, remove and re-add torrent handle using known metadata, then schedule the new window.
7. Show buffering overlay until enough bytes are available.

### Cache States

Track each piece in app state:

- `missing`: no bytes retained.
- `requested`: deadline/priority set, waiting for peers.
- `partial`: some blocks stored.
- `verified`: full piece passed hash check.
- `retained`: verified and inside current cache window.
- `evictable`: verified but outside current cache window.
- `evicted`: removed from RAM.
- `failed`: hash failed or read error.

## UI Direction

Intent:

- User wants to start watching a film quickly from a torrent source.
- UI should feel like a media player with torrent visibility, not like a full torrent client.

Screens:

1. Title/Input screen
   - Drop zone.
   - Magnet input.
   - Open file button.
   - Recent sources later, not MVP.

2. Torrent loading screen
   - Metadata status.
   - Tracker/DHT/peer indicators.
   - Clear error messages.

3. File selection screen
   - Media-first list of likely video files.
   - Path, size, extension, progress/availability.
   - Optional "show all files" toggle.

4. Player screen
   - Video surface.
   - Clay overlay controls.
   - Timeline with torrent piece rail.
   - Buffer setting in player settings.
   - Buffer usage indicator.

Signature element:

- Piece rail under timeline showing played, retained, downloading, missing, and evicted segments.

## Staged Implementation Plan

Status legend:

- `[ ]` Not started.
- `[~]` In progress.
- `[x]` Complete.
- `[!]` Blocked or needs decision.

### Stage 0: Research and Architecture

Status: `[x]`

Deliverables:

- [x] Research libmpv embedding and custom streams.
- [x] Research Clay role and limitations.
- [x] Research SDL3 native dialogs and drag/drop.
- [x] Research libtorrent 2.x custom disk subsystem.
- [x] Identify bounded RAM cache as the main technical risk.

Exit criteria:

- Architecture and staged plan are documented.

### Stage 1: Project Scaffold

Status: `[x]`

Tasks:

- [x] Create Meson project structure.
- [x] Add dependency discovery for SDL3, OpenGL, libmpv, libtorrent, and fonts.
- [x] Vendor or submodule Clay.
- [x] Add formatting/linting conventions.
- [x] Add minimal CI or local build script.

Exit criteria:

- App builds and opens an empty native window.

### Stage 2: Native Window and Input Shell

Status: `[x]`

Tasks:

- [x] Create SDL3 window with OpenGL context.
- [x] Implement main loop.
- [x] Handle resize, DPI scale, quit, keyboard, mouse, wheel.
- [x] Implement drag/drop for torrent files and dropped text.
- [x] Implement clipboard paste for magnet links.
- [x] Implement native file picker with `.torrent` filter.

Exit criteria:

- App can accept a file path or magnet string and display the parsed input type.

### Stage 3: Clay UI Foundation

Status: `[~]`

Tasks:

- [x] Initialize Clay arena and error handling.
- [x] Implement ThorVG/OpenGL renderer for core Clay primitives.
- [x] Add font loading and text measurement.
- [x] Implement input hit testing and widget state for the title screen.
- [~] Build base components: button, text input, list row, scroll region, slider, toggle, menu.
- [x] Build title/input screen.

Exit criteria:

- User can interact with title screen using custom Clay UI.

### Stage 4: libmpv Local Playback Prototype

Status: `[x]`

Tasks:

- [x] Create `MpvPlayer`.
- [x] Initialize mpv with embedded-friendly options.
- [x] Create mpv OpenGL render context.
- [x] Render local video into app window.
- [x] Add basic play/pause/seek/volume/fullscreen controls.
- [x] Observe `time-pos`, `duration`, `pause`, `eof-reached`, and track properties.

Exit criteria:

- App plays a normal local video file through libmpv inside the custom window.

### Stage 5: Torrent Metadata and File Browser

Status: `[x]`

Tasks:

- [x] Create `TorrentService`.
- [x] Configure libtorrent session settings.
- [x] Load `.torrent` files.
- [x] Parse magnet links and wait for metadata.
- [x] Process alerts on a bridge loop.
- [x] Extract file list from `torrent_info`.
- [x] Detect likely video files.
- [x] Build file selection screen.

Exit criteria:

- User can open `.torrent` or magnet and see selectable video files.

Implementation note:

- Code is guarded for builds without libtorrent and was compile-verified in that configuration.
  Runtime metadata loading requires a build where `libtorrent-rasterbar` is found.

### Stage 6: RAM Disk Interface Prototype

Status: `[x]`

Tasks:

- [x] Implement `MemoryDiskIO` from libtorrent `disk_interface`.
- [x] Store incoming blocks in RAM by storage index and piece index.
- [x] Implement reads from RAM.
- [x] Implement v1 SHA-1 and v2 SHA-256 hash paths.
- [x] Implement status, delete, release, stop, and no-op move/rename behavior.
- [x] Track memory usage.
- [x] Add test torrent fixture or local integration test.

Exit criteria:

- Libtorrent can download and verify pieces into RAM without writing payload files to disk.

### Stage 7: Bounded Rolling Cache Prototype

Status: `[ ]`

Tasks:

- [ ] Add `CachePolicy` with default `128 MiB`.
- [ ] Add player UI control for cache size.
- [ ] Compute retained piece window from playback byte offset.
- [ ] Evict pieces outside retained window in `MemoryDiskIO`.
- [ ] Detect storage miss for evicted pieces.
- [ ] Prototype backward seek outside cache.
- [ ] Verify whether remove/re-add torrent handle is required for clean redownload.
- [ ] Verify upload-disabled behavior does not trigger disk errors for evicted pieces.

Exit criteria:

- Cache remains near configured limit during forward playback.
- Seeking within retained range is immediate.
- Seeking outside retained range has a defined and tested rebuffer path.

### Stage 8: mpv Torrent Stream Bridge

Status: `[ ]`

Tasks:

- [ ] Register `torrview://` protocol with `mpv_stream_cb_add_ro`.
- [ ] Implement stream open and cookie lifetime.
- [ ] Implement blocking read with cancellation.
- [ ] Implement seek and size.
- [ ] Map stream reads to torrent pieces.
- [ ] Wake reads on piece completion alerts.
- [ ] Handle end-of-file and read errors.

Exit criteria:

- mpv can open and read selected torrent file through app stream callbacks.

### Stage 9: Streaming Scheduler

Status: `[ ]`

Tasks:

- [ ] Set initial file priorities.
- [ ] Set urgent piece deadlines for active reads.
- [ ] Maintain ahead buffer with staggered deadlines.
- [ ] Clear stale deadlines on seek.
- [ ] Set outside-window priorities to zero.
- [ ] Track piece state for UI rail.
- [ ] Add stalled-piece timeout behavior.
- [ ] Add peer/speed aware buffering status.

Exit criteria:

- App can start playback before full file download and continue with bounded RAM usage.

### Stage 10: Player UI and UX Completion

Status: `[ ]`

Tasks:

- [ ] Build player overlay controls.
- [ ] Build torrent piece rail.
- [ ] Build buffer settings menu.
- [ ] Build loading, buffering, stalled, and error overlays.
- [ ] Add audio track menu.
- [ ] Add fullscreen behavior.
- [ ] Add keyboard shortcuts.

Exit criteria:

- Playback flow is usable without relying on mpv's default OSC.

### Stage 11: Robustness and Edge Cases

Status: `[ ]`

Tasks:

- [ ] Magnet with slow metadata.
- [ ] Torrent with many files.
- [ ] Multi-file torrents with video split across directories.
- [ ] v1, v2, and hybrid torrents.
- [ ] Large piece sizes.
- [ ] Sparse or unhealthy swarms.
- [ ] Network loss and resume.
- [ ] Hash failure.
- [ ] Cache limit smaller than piece size.
- [ ] User closes app during blocked read.
- [ ] User opens a new torrent during playback.

Exit criteria:

- App fails clearly and recovers without deadlocks or corrupt state.

### Stage 12: Packaging

Status: `[ ]`

Tasks:

- [ ] Decide license/distribution strategy for mpv build.
- [ ] Define Linux dependency policy using system packages where practical.
- [ ] Define Windows dependency bundle layout.
- [ ] Statically link dependencies where practical and license-compatible.
- [ ] Build custom FFmpeg with only required video playback features.
- [ ] Build custom mpv/libmpv against the custom FFmpeg for packaged builds.
- [ ] Package Windows build.
- [ ] Package Linux build.
- [ ] Add first-run dependency diagnostics.

Exit criteria:

- App can be installed/run on Windows and Linux without local development dependencies beyond the chosen Linux system packages.

## MVP Definition

MVP includes:

- Native desktop window.
- Drag/drop `.torrent`.
- Paste magnet link.
- Native file picker for `.torrent`.
- Torrent metadata loading.
- Video file selection.
- libmpv playback from torrent-backed stream.
- Strict RAM-backed payload storage.
- Configurable rolling buffer with `128 MiB` default.
- Basic player controls.
- Buffering/error states.

MVP excludes:

- Persistent library/history.
- Full torrent client controls.
- Saving downloaded media.
- Streaming multiple video files at once.
- Arbitrary seamless backward seek outside retained cache.
- Auto-loading subtitle sidecars from torrent contents.
- Remote casting.

## Closed Decisions

- Target platforms now: Windows and Linux.
- Disk cache: no disk cache, including no optional temp-cache mode.
- Uploads: disabled.
- Packaging: Linux should use system dependencies where practical; Windows should bundle runtime libraries.
- Linking: statically link as much as practical while respecting licenses and platform constraints.
- FFmpeg: use a custom build with only required video playback features for packaged builds.
- Subtitle sidecars: not in MVP.

## Research Sources

- Clay README: https://github.com/nicbarker/clay
- SDL3 file dialog: https://wiki.libsdl.org/SDL3/SDL_ShowOpenFileDialog
- SDL3 drop event: https://wiki.libsdl.org/SDL3/SDL_DropEvent
- mpv manual: https://mpv.io/manual/master/
- mpv client API header: https://raw.githubusercontent.com/mpv-player/mpv/master/include/mpv/client.h
- mpv render API header: https://raw.githubusercontent.com/mpv-player/mpv/master/include/mpv/render.h
- mpv OpenGL render API header: https://raw.githubusercontent.com/mpv-player/mpv/master/include/mpv/render_gl.h
- mpv custom stream callbacks: https://raw.githubusercontent.com/mpv-player/mpv/master/include/mpv/stream_cb.h
- libtorrent streaming notes: https://raw.githubusercontent.com/arvidn/libtorrent/RC_2_0/docs/streaming.rst
- libtorrent disk interface: https://raw.githubusercontent.com/arvidn/libtorrent/RC_2_0/include/libtorrent/disk_interface.hpp
- libtorrent torrent handle: https://raw.githubusercontent.com/arvidn/libtorrent/RC_2_0/include/libtorrent/torrent_handle.hpp
- libtorrent session params: https://raw.githubusercontent.com/arvidn/libtorrent/RC_2_0/include/libtorrent/session_params.hpp
- libtorrent custom storage example: https://raw.githubusercontent.com/arvidn/libtorrent/RC_2_0/examples/custom_storage.cpp
