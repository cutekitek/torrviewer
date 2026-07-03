#!/usr/bin/env bash
set -euo pipefail

builddir="${1:-build}"

if [[ -d "${builddir}" ]]; then
  meson setup "${builddir}" --reconfigure
else
  meson setup "${builddir}"
fi

meson compile -C "${builddir}"
