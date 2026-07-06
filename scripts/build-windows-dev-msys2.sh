#!/usr/bin/env bash
set -euo pipefail

builddir="${1:-build-windows-dev}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_path="${repo_root}/${builddir}"

case "${MSYSTEM:-}" in
  UCRT64|MINGW64|CLANG64) ;;
  *)
    echo "Run from an MSYS2 MinGW shell, preferably UCRT64." >&2
    echo "PowerShell helper: ./scripts/build-windows-dev.ps1" >&2
    exit 1
    ;;
esac

meson_options=(
  --buildtype=debugoptimized
  --wrap-mode=nofallback
  -Dwindows_static=false
  -Dmpv=auto
  -Dsystem_mpv=true
  -Dlibtorrent=auto
  -Dsystem_libtorrent=true
  -Dthorvg=auto
  -Dfreetype=auto
  -Dfontconfig=auto
)

if [[ -d "${build_path}" ]]; then
  meson setup "${build_path}" --reconfigure "${meson_options[@]}"
else
  meson setup "${build_path}" "${repo_root}" "${meson_options[@]}"
fi

meson compile -C "${build_path}" torrview-dev

echo
echo "Built ${build_path}/src/torrview-dev.exe"
