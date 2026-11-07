# Windows Static Build

The Windows static profile is configured and built directly with Meson. Run all
commands from the **MSYS2 CLANG64** shell. Keep Meson, pkg-config, Clang,
CMake, and all dependency packages in the CLANG64 prefix. Verify the shell
before configuring:

```sh
test "$MSYSTEM" = CLANG64
command -v meson clang clang++ llvm-ar pkg-config cmake
export CC=clang CXX=clang++ AR=llvm-ar RANLIB=llvm-ranlib
```

All tools should resolve under `/clang64/bin`. Do not add `/ucrt64/bin` or
`/mingw64/bin` to this shell's `PATH` or reuse their pkg-config paths: CLANG64
uses libc++, while the GCC environments use libstdc++.

Install the build tools and system development libraries if needed:

```sh
pacman -S --needed mingw-w64-clang-x86_64-toolchain \
  mingw-w64-clang-x86_64-meson mingw-w64-clang-x86_64-ninja \
  mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-pkgconf \
  mingw-w64-clang-x86_64-sdl3 mingw-w64-clang-x86_64-sqlite3 \
  mingw-w64-clang-x86_64-freetype mingw-w64-clang-x86_64-fontconfig \
  mingw-w64-clang-x86_64-boost mingw-w64-clang-x86_64-openssl
```

Initialize the Clay and libplacebo submodules before the first build:

```sh
git submodule update --init --recursive
```

Meson builds FFmpeg, libplacebo, libmpv, libtorrent-rasterbar, and ThorVG as
static subprojects. FFmpeg is a decode-only, no-network build containing the
formats and codecs used by the application. Libplacebo contains only its
OpenGL backend. The `prod` target makes the complete application dependency
set mandatory even with `-Dauto_features=disabled`. No external
FFmpeg/libplacebo prefix is required.

Configure and build the O2-optimized executable:

```sh
meson setup build-prod -Dbuild_target=prod

meson compile -C build-prod prod
```

The production defaults—including O2 optimization, stripping, static libraries,
static preference, disabled dependency autodetection, non-PIC static objects,
and FFmpeg's required PIC exception—are defined in the project Meson files.

The static profile compiles functions and data into separate sections and
garbage-collects unused sections at link time. LTO is left off by default.

If the build directory was already configured with another compiler or
dependency source, regenerate it from the CLANG64 shell with the compiler
variables above set. Reconfiguring alone does not switch compilers or replace
old dependency objects:

```sh
meson setup --wipe build-prod -Dbuild_target=prod
meson compile -C build-prod prod
```

The remaining profile options are preserved by `--wipe`. This discards the
generated build tree, so save any runtime settings or SQLite library you placed
inside it before switching toolchains.

The executable is written to `build-prod/src/prod.exe`. It is a
Windows GUI application, so it does not open a console window.

After the initial setup, `meson compile` automatically regenerates the build
files when Meson inputs change. Use `meson configure build-prod` to
view or change options, and use `meson setup --wipe build-prod` when
switching compilers or dependency sources.

## Android development on Windows

The Compose Android/TV application uses Gradle for its Android package and Meson for
its shared C++ library. See [Android builds](android.md) for Windows NDK and WSL
dependency-build instructions. Desktop target commands in this guide are unchanged.

## SQLite library

Desktop builds require SQLite 3 development headers and libraries (`sqlite3` in
pkg-config). On MSYS2 CLANG64 install `mingw-w64-clang-x86_64-sqlite3`; production
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
