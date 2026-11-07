# Torrview Agent Guide

## Scope

These instructions apply to the entire repository. Torrview is a C++20 desktop media player that
opens local media and streams files from torrents without using a conventional on-disk torrent
payload store. SDL3 owns the window and event loop, OpenGL presents the UI and video, Clay defines
the immediate-mode layout, ThorVG renders text and SVG assets, libmpv handles playback, and
libtorrent-rasterbar handles torrent sessions.

Keep changes focused on first-party code. Preserve unrelated work in the working tree; this
repository may already contain uncommitted build-system or dependency changes.

## Repository map

- `src/main.cpp`: process entry point; constructs and runs `torrview::Application`.
- `src/app.*`: application lifetime, SDL/OpenGL setup, frame loop, navigation, and coordination of
  playback, torrent state, settings, and UI.
- `src/ui/`: Clay pages and rendering. `ui::App` owns registered pages; `TitlePage`,
  `FileBrowserPage`, `SettingsPage`, and `PlayerOverlay` are the current screens.
- `src/mpv_player.*`: libmpv wrapper and the `PlaybackSnapshot` exposed to the UI.
- `src/torrent_service.*`: libtorrent session, metadata/file selection, streaming state, and the
  `TorrentSnapshot` exposed to the UI.
- `src/torrent_stream.hpp`: narrow stream-provider/reader boundary between libtorrent and libmpv.
- `src/memory_disk_io.*` and `src/cache_policy.*`: memory-backed piece storage, retained playback
  window, and eviction policy. Treat cache accounting and piece-state transitions as invariants.
- `src/app_settings.*` and `src/settings.*`: clamped INI-backed settings stored as `torrview.ini`
  beside the executable.
- `src/input_source.*`: classification of local media paths, `.torrent` files, magnet links, and
  other user input.
- `assets/`: embedded font and SVG controls.
- `scripts/pack_resources.py`: generates `resources.cpp` and `resources.hpp` during the build.
- `meson.build`, `meson_options.txt`, and `src/meson.build`: all supported build definitions.
- `docs/build.md`: canonical cross-platform build instructions; the two Windows documents add
  MSYS2-specific guidance.
- `third_party/clay/` and `subprojects/`: submodules, wraps, downloaded dependencies, or dependency
  patches. Do not edit dependency source for an application change. Changes under
  `subprojects/packagefiles/` are only appropriate when intentionally maintaining a Meson fallback.
- `build/`, `build-*`, and `builddir/`: generated build trees. Never edit or commit their contents.

## Architecture and behavior

The main loop pumps SDL events, consumes file-dialog results, processes libmpv events and
libtorrent alerts, builds one Clay frame, renders it, and swaps the OpenGL window. Keep this path
responsive: do not add blocking filesystem, network, or metadata work to a frame or event handler.

Input follows two paths:

- Local media resets torrent state and loads the path directly in `MpvPlayer`.
- A torrent file or magnet stops current playback, loads metadata through `TorrentService`, lets the
  user select a file, and exposes that file to libmpv through the custom torrent stream provider.

Service/UI boundaries are snapshot-based. Add playback state to `PlaybackSnapshot` and torrent
state to `TorrentSnapshot` rather than letting pages reach into backend implementation details.
`TorrentService` and `MemoryDiskIO` use PIMPLs to contain dependency-heavy implementation and
conditional compilation; preserve that separation.

The torrent payload cache is intentionally memory-backed. When changing reads, writes, seeks, or
eviction, preserve piece bounds, byte accounting, retained-window behavior, and callback completion
semantics. Do not silently fall back to persistent torrent payload storage.

Pages are registered in `Application::register_pages`. A new page normally needs a `.hpp`/`.cpp`
pair in `src/ui/`, inheritance from `ui::Page`, registration in `Application`, and inclusion in
`torrview_sources` in `src/meson.build`.

## Dependencies and feature gates

libmpv is mandatory in every profile; there is no option or macro for building without it.
Meson generates `config.hpp` from `src/config.hpp.in`. Optional development dependencies are
represented by `TORRVIEW_HAVE_LIBTORRENT`, `TORRVIEW_HAVE_FREETYPE`, `TORRVIEW_HAVE_FONTCONFIG`,
`TORRVIEW_HAVE_THORVG`, and `TORRVIEW_HAVE_TRACY`.

- Use the generated macros; do not hard-code local dependency availability.
- Preserve compilable and useful unavailable-feature paths in development builds. The wrappers'
  `available()` methods and status snapshots are the UI-facing capability checks.
- The `prod` profile makes the complete application dependency set mandatory and builds bundled
  static fallbacks. Dependency option changes must be checked against both `dev` and `prod` logic.
- Tracy remains opt-in developer instrumentation. Allocation hooks are additionally controlled by
  `tracy_allocations` and must remain compatible with the documented ThorVG constraint.

## Build commands

Meson is the only supported build system. Initialize dependencies once:

```sh
git submodule update --init --recursive
```

For a new development build, use the profile from `docs/build.md`:

```sh
meson setup build-dev \
  --buildtype=debugoptimized \
  --default-library=shared \
  --wrap-mode=nofallback \
  -Dstrip=false \
  -Dprefer_static=false \
  -Dauto_features=auto \
  -Db_staticpic=true \
  -Db_ndebug=false \
  -Dbuild_target=dev \
  -Dsystem_mpv=true \
  -Dlibtorrent=auto \
  -Dsystem_libtorrent=true \
  -Dthorvg=auto \
  -Dfreetype=auto \
  -Dfontconfig=auto
meson compile -C build-dev dev
```

On Windows, run build commands from an MSYS2 MinGW shell, preferably UCRT64, so Meson, pkg-config,
the compiler, and runtime DLLs come from one toolchain. The development executable is
`build-dev/src/dev.exe` and intentionally uses the console subsystem for logs.
  
Production builds are substantially more expensive because they build the bundled dependency
graph:

```sh
meson setup build-prod -Dbuild_target=prod
meson compile -C build-prod prod
```

Run configured targets with `meson compile -C build-dev run-dev` or
`meson compile -C build-prod run-prod`. Use `meson setup --reconfigure <build-dir>` after changing
ordinary Meson inputs. Use `meson setup --wipe <build-dir>` only when the toolchain or dependency
source changed; a wipe destroys that generated build tree.

## Validation

There are currently no first-party automated test targets in the active Meson files. Do not report
`meson test` as meaningful unless the change adds or restores a test target.

For application changes:

1. Format only touched C/C++ files with the repository `.clang-format`.
2. Compile the `dev` target in an existing or newly configured development tree.
3. Smoke-test the relevant GUI flow with `run-dev`: startup, input classification, page navigation,
   and any affected local-file or torrent playback path.
4. Compile `prod` when changing dependency selection, static linkage, generated resources,
   conditional compilation, Windows subsystem behavior, or production-only options.

For pure logic, add a small first-party Meson test when practical. Register it with Meson's
`test()` function and run it with:

```sh
meson test -C build-dev --print-errorlogs
```

If a dependency-gated implementation changes, validate the feature-enabled build and, where the
development profile permits it, a feature-disabled build. Never use successful compilation of
vendored dependency tests as a substitute for exercising Torrview code.

## C++ conventions

- Follow `.editorconfig`: UTF-8, LF, final newline, spaces, and two-space indentation.
- Follow `.clang-format`: LLVM base style, 100-column limit, left-aligned pointers, sorted includes.
- Code is C++20. Keep it warning-clean under `-Wpedantic`, `-Wconversion`, `-Wsign-conversion`, and
  `-Wshadow`; make narrowing and signed/unsigned conversions explicit.
- Use `torrview` and `torrview::ui` namespaces. Existing naming is `PascalCase` for types,
  `snake_case` for functions and variables, and a trailing underscore for private data members.
- Prefer RAII and explicit ownership. Backend wrappers are non-copyable and own C-library handles;
  keep destruction order and shutdown callbacks safe.
- Use `[[nodiscard]]` for value-returning helpers where ignoring the result is likely a bug.
- Report runtime failures through the `TORRVIEW_LOG_*` macros. Throw exceptions for initialization
  or operations that cannot proceed, and keep optional-feature absence as a normal capability state.
- Keep headers narrow. Forward-declare C-library handles where possible and isolate heavy
  libtorrent/libmpv details in implementation files.

## UI and resource changes

Clay is immediate-mode: page state belongs on the page object, while each `draw()` call describes
the full current layout. Route global actions such as navigation, fullscreen, settings application,
and quitting through `Application` rather than duplicating ownership in pages.

All bundled assets must be added both under `assets/` and to `resource_files` in the root
`meson.build`. `src/meson.build` invokes the resource packer and generates files in the build tree;
never hand-edit generated `resources.cpp`, `resources.hpp`, or `config.hpp`.

## Build-system and documentation changes

- Add first-party `.cpp` files to `torrview_sources` in `src/meson.build`.
- Keep the `dev`/`prod` target names and `run-dev`/`run-prod` conventions consistent.
- Preserve the production profile's static/decode-only intent and its platform-specific branches.
- Update `docs/build.md` and the relevant Windows guide when commands, required packages, output
  paths, profiles, or Meson options change.
- Do not reintroduce legacy Makefile or wrapper-script instructions; the current project explicitly
  supports direct Meson commands only.

## Completion checklist

Before handing off a change, inspect the diff for accidental edits to subprojects or generated
files, ensure new sources/assets are wired into Meson, state exactly which compile/test/smoke checks
ran, and call out any dependency-gated path that could not be exercised locally.
