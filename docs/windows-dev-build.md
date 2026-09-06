# Windows Development Build

Use an MSYS2 MinGW shell, preferably UCRT64, with Meson, Ninja, a C++ compiler,
pkg-config, SDL3, and the optional application dependencies installed.

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
