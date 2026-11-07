# Windows Development Build

Use an MSYS2 MinGW shell, preferably UCRT64, with Meson, Ninja, a C++ compiler,
pkg-config, SDL3, and the optional application dependencies installed.
Production uses [MSYS2 CLANG64](windows-static-build.md); keep its `build-prod`
directory separate from a UCRT64 development build.

Configure the development build directly with Meson:

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
```

Build and run the console application with Meson:

```sh
meson compile -C build-dev dev
meson compile -C build-dev run-dev
```

After the initial setup, `meson compile` automatically regenerates the build
files when Meson inputs change. Use `meson configure build-dev` to view
or change options.

The executable is written to
`build-dev/src/dev.exe`. Run it from the same MSYS2 shell so the matching
runtime DLLs are available. It is a console application so development logs
remain visible.

## Android development on Windows

The Compose Android/TV application uses Gradle for its Android package and Meson for
its shared C++ library. See [Android builds](android.md) for Windows NDK and WSL
dependency-build instructions. Desktop target commands in this guide are unchanged.

## SQLite library

Desktop builds require SQLite 3 development headers and libraries (`sqlite3` in
pkg-config). On MSYS2 UCRT64 install `mingw-w64-ucrt-x86_64-sqlite3`; production
uses its static archive. Linux distributions typically package the headers as
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
