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
