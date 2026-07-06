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

copy_runtime_dlls() {
  local exe="$1"
  local exe_dir
  exe_dir="$(dirname "${exe}")"
  local mingw_prefix="${MINGW_PREFIX:-}"
  if [[ -z "${mingw_prefix}" ]]; then
    case "${MSYSTEM:-}" in
      UCRT64) mingw_prefix="/ucrt64" ;;
      MINGW64) mingw_prefix="/mingw64" ;;
      CLANG64) mingw_prefix="/clang64" ;;
      *) mingw_prefix="/ucrt64" ;;
    esac
  fi
  local prefix_bin="${mingw_prefix}/bin"

  if ! command -v objdump >/dev/null 2>&1; then
    echo "objdump not found; skipping runtime DLL copy" >&2
    return 0
  fi

  local -A seen_dlls=()
  local -a scan_queue=("${exe}")
  local -a runtime_dlls=()

  while ((${#scan_queue[@]} > 0)); do
    local binary="${scan_queue[0]}"
    scan_queue=("${scan_queue[@]:1}")

    while IFS= read -r dll_name; do
      local dll_key="${dll_name,,}"
      if [[ -n "${seen_dlls[${dll_key}]:-}" ]]; then
        continue
      fi
      seen_dlls["${dll_key}"]=1

      local dll_path="${prefix_bin}/${dll_name}"
      if [[ ! -f "${dll_path}" ]]; then
        dll_path="$(find "${prefix_bin}" -maxdepth 1 -iname "${dll_name}" -print -quit)"
      fi

      if [[ -f "${dll_path}" ]]; then
        runtime_dlls+=("${dll_path}")
        scan_queue+=("${dll_path}")
      fi
    done < <(objdump -p "${binary}" 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name: //p')
  done

  for dll_path in "${runtime_dlls[@]}"; do
    cp -u "${dll_path}" "${exe_dir}/"
  done

  if command -v ldd >/dev/null 2>&1; then
    local missing_dlls
    missing_dlls="$(ldd "${exe}" | sed -n '/=> not found/p')"
    if [[ -n "${missing_dlls}" ]]; then
      echo "Missing runtime dependencies:" >&2
      echo "${missing_dlls}" >&2
      return 1
    fi
  fi

  echo "Staged ${#runtime_dlls[@]} runtime DLLs in ${exe_dir}"
}

if [[ -d "${build_path}" ]]; then
  meson setup "${build_path}" --reconfigure "${meson_options[@]}"
else
  meson setup "${build_path}" "${repo_root}" "${meson_options[@]}"
fi

meson compile -C "${build_path}" torrview-dev

copy_runtime_dlls "${build_path}/src/torrview-dev.exe"

echo
echo "Built ${build_path}/src/torrview-dev.exe"
