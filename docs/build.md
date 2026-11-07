# Building with Meson

Meson is the only supported build system. Initialize the Git submodules before
the first build:

```sh
git submodule update --init --recursive
```

The project provides two host-native executable targets:

- `dev` uses installed development dependencies where available.
- `prod` always enables the complete application feature set, builds FFmpeg,
  libplacebo, libmpv, libtorrent-rasterbar, and ThorVG as bundled static
  dependencies, and enables O2 compiler optimization with size-oriented linker
  settings.

Tracy is optional developer instrumentation rather than an application
feature, so it remains opt-in with `-Dtracy=enabled`.

Configure and compile the development target:

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
  -Dmpv=auto \
  -Dsystem_mpv=true \
  -Dlibtorrent=auto \
  -Dsystem_libtorrent=true \
  -Dthorvg=auto \
  -Dfreetype=auto \
  -Dfontconfig=auto

meson compile -C build-dev dev
```

On Windows, use the MSYS2 **CLANG64** shell for production and keep all tools
and system dependencies in `/clang64`. Set `CC=clang`, `CXX=clang++`,
`AR=llvm-ar`, and `RANLIB=llvm-ranlib` before setup; see the
[CLANG64 production guide](windows-static-build.md). When switching an existing
build directory from GCC, use `meson setup --wipe build-prod -Dbuild_target=prod`
from that environment so cached dependency objects are rebuilt.

Configure and compile the production target:

```sh
meson setup build-prod -Dbuild_target=prod

meson compile -C build-prod prod
```

The production defaults—including O2 optimization, stripping, static libraries,
static preference, disabled dependency autodetection, non-PIC static objects,
and FFmpeg's required PIC exception—are defined in `meson.build`.

Use `meson compile -C build-dev run-dev` or
`meson compile -C build-prod run-prod` to run a configured target.

After the initial setup, `meson compile` automatically regenerates the build
files when Meson inputs change. Useful maintenance commands are:

```sh
meson configure build-dev
meson setup --reconfigure build-dev
meson setup --wipe build-dev
```

Use `meson configure` to list project options before configuration, or
`meson configure <build-directory>` afterward. Windows-specific details are
documented in [Windows development build](windows-dev-build.md) and
[Windows production build](windows-static-build.md).

## Android

For the Jetpack Compose Android/TV application and shared native engine, see
[Android builds](android.md). Desktop builds continue to use the `dev`/`prod`
and `run-dev`/`run-prod` Meson targets.

## SQLite library

Desktop builds require SQLite 3 development headers and libraries (`sqlite3` in
pkg-config). On MSYS2 UCRT64 install `mingw-w64-ucrt-x86_64-sqlite3`; production
uses its static archive (the CLANG64 production package is
`mingw-w64-clang-x86_64-sqlite3`). Linux distributions typically package the headers as
`libsqlite3-dev` or `sqlite-devel`.

The desktop library lives in `torrview.sqlite` beside the executable, alongside
`torrview.ini`. Keep that directory writable. The database includes torrent
metadata, source links, display names, the last watched file, and per-file
positions in seconds. Torrent payloads remain memory-backed. SQLite writes run
in a background worker, with progress saved every second and flushed on normal
shutdown. An abrupt process termination can lose the latest checkpoint.

Click a library name to browse all files; click Play/Resume to watch the last
selected episode at its saved position. Each video in the file list resumes its
own position. The `...` menu opens Rename and Delete actions with confirmation
modals. Delete removes library metadata and progress only.

## Capturing UI screenshots

Any desktop build has a scripted capture mode for reviewing the interface without
touching the mouse or keyboard. When `TORRVIEW_UI_CAPTURE` names a directory, the
window stays hidden, the app walks through the library, file list, settings,
dialogs, and player states using its real input handlers, writes one BMP per
state into that directory, and quits.

```sh
TORRVIEW_UI_CAPTURE=/tmp/torrview-shots \
TORRVIEW_UI_CAPTURE_TORRENT=/path/to/test.torrent \
TORRVIEW_UI_CAPTURE_VIDEO=/path/to/short-video.mkv \
  build-dev/src/dev.exe
```

Both media variables are optional; the steps that need them are skipped when they
are unset. The run uses the normal `torrview.ini` and `torrview.sqlite` beside the
executable, so delete `torrview.sqlite` first to capture the empty library. The
script lives in `Application::dev_capture_setup()` and finds controls by their
Clay element IDs. A short video with two audio tracks can be generated with:

```sh
ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=30 -f lavfi -i sine=frequency=440 \
  -f lavfi -i sine=frequency=660 -t 60 -map 0:v -map 1:a -map 2:a \
  -c:v libx264 -c:a aac short-video.mkv
```
