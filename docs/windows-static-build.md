# Windows Static Build

The Windows static profile is configured and built directly with Meson. Run all
commands from the **MSYS2 UCRT64** shell, not the plain MSYS shell or
PowerShell. Verify the shell before configuring:

```sh
test "$MSYSTEM" = UCRT64
command -v meson gcc g++
```

All three commands must succeed. An empty compiler error during `meson setup`
usually means the UCRT64 toolchain was launched with an incomplete `PATH`.

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
garbage-collects unused sections at link time. LTO is intentionally left off:
GCC 16.1 for MinGW currently crashes while linking this dependency graph. It
can be tested with `-Db_lto=true` on another compiler version.

If the build directory was already configured with another compiler or
dependency source, remove its cached configuration and repeat the full setup
command above:

```sh
meson setup --wipe build-prod
```

The remaining profile options are preserved by `--wipe`.

The executable is written to `build-prod/src/prod.exe`. It is a
Windows GUI application, so it does not open a console window.

After the initial setup, `meson compile` automatically regenerates the build
files when Meson inputs change. Use `meson configure build-prod` to
view or change options, and use `meson setup --wipe build-prod` when
switching compilers or dependency sources.
