#!/usr/bin/env bash
set -euo pipefail

builddir="${1:-build-windows-static}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps_root="${repo_root}/build/_windows-static-deps"
prefix="${deps_root}/prefix"
ffmpeg_src="${deps_root}/ffmpeg"
libplacebo_src="${deps_root}/libplacebo"
libplacebo_build="${deps_root}/libplacebo-build"
ffmpeg_ref="${FFMPEG_REF:-n7.1.1}"
libplacebo_ref="${LIBPLACEBO_REF:-v7.360.1}"
log_dir="${repo_root}/build/logs"
log_file="${log_dir}/windows-static-$(date +%Y%m%d-%H%M%S).log"

mkdir -p "${log_dir}"
exec > >(tee -a "${log_file}") 2>&1

echo "Logging to ${log_file}"

case "${MSYSTEM:-}" in
  UCRT64) package_prefix="mingw-w64-ucrt-x86_64" ;;
  MINGW64) package_prefix="mingw-w64-x86_64" ;;
  CLANG64) package_prefix="mingw-w64-clang-x86_64" ;;
  *)
    echo "Run from an MSYS2 MinGW shell, preferably UCRT64." >&2
    echo "PowerShell helper: ./scripts/build-windows-static.ps1" >&2
    exit 1
    ;;
esac

install_packages() {
  if command -v pacman >/dev/null 2>&1; then
    pacman -S --needed --noconfirm \
      git make perl nasm yasm diffutils pkgconf \
      "${package_prefix}-binutils" \
      "${package_prefix}-gcc" \
      "${package_prefix}-make" \
      "${package_prefix}-cmake" \
      "${package_prefix}-meson" \
      "${package_prefix}-ninja" \
      "${package_prefix}-pkgconf" \
      "${package_prefix}-python" \
      "${package_prefix}-python-jinja" \
      "${package_prefix}-sdl3" \
      "${package_prefix}-freetype" \
      "${package_prefix}-fontconfig" \
      "${package_prefix}-libass" \
      "${package_prefix}-vulkan-headers" \
      "${package_prefix}-zlib" \
      "${package_prefix}-bzip2" \
      "${package_prefix}-openssl" \
      "${package_prefix}-boost"
  fi
}

build_ffmpeg() {
  mkdir -p "${deps_root}" "${prefix}"
  local stamp="${prefix}/.ffmpeg-${ffmpeg_ref}.stamp"

  if [[ -f "${stamp}" && -f "${prefix}/lib/pkgconfig/libavcodec.pc" ]]; then
    echo "Using existing FFmpeg build for ${ffmpeg_ref}"
    return
  fi

  if [[ -d "${ffmpeg_src}/.git" && ! -f "${ffmpeg_src}/configure" ]]; then
    echo "Removing interrupted FFmpeg checkout at ${ffmpeg_src}"
    rm -rf "${ffmpeg_src}"
  fi

  if [[ ! -d "${ffmpeg_src}/.git" ]]; then
    git clone --depth 1 --branch "${ffmpeg_ref}" https://github.com/FFmpeg/FFmpeg.git "${ffmpeg_src}"
  else
    git -C "${ffmpeg_src}" fetch --depth 1 origin "refs/tags/${ffmpeg_ref}:refs/tags/${ffmpeg_ref}"
    git -C "${ffmpeg_src}" checkout --detach "${ffmpeg_ref}^{commit}"
  fi

  pushd "${ffmpeg_src}" >/dev/null
  make distclean >/dev/null 2>&1 || true

  ./configure \
    --prefix="${prefix}" \
    --pkg-config=pkg-config \
    --enable-static \
    --disable-shared \
    --disable-programs \
    --disable-doc \
    --disable-debug \
    --disable-network \
    --disable-autodetect \
    --disable-everything \
    --enable-avcodec \
    --enable-avformat \
    --enable-avfilter \
    --enable-avutil \
    --enable-swresample \
    --enable-swscale \
    --enable-zlib \
    --enable-protocol=file,pipe \
    --enable-demuxer=matroska,mov,avi,mpegts,mpeg,mpegvideo,flv,ogg,wav,mp3,aac,flac \
    --enable-parser=aac,aac_latm,ac3,av1,h264,hevc,mpeg4video,mpegaudio,mpegvideo,opus,vorbis,vp8,vp9 \
    --enable-decoder=aac,aac_latm,ac3,eac3,alac,flac,mp1,mp2,mp3,opus,vorbis,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,h264,hevc,mpeg1video,mpeg2video,mpeg4,msmpeg4v2,msmpeg4v3,h263,vc1,wmv1,wmv2,wmv3,vp8,vp9,av1,theora \
    --enable-filter=abuffer,abuffersink,aformat,anull,aresample,buffer,buffersink,format,null,scale

  make -j"$(nproc)"
  make install
  touch "${stamp}"
  popd >/dev/null
}

sanitize_libplacebo_pc() {
  local pc_file="${prefix}/lib/pkgconfig/libplacebo.pc"
  if [[ -f "${pc_file}" ]]; then
    sed -i \
      -e '/^Requires: vulkan$/d' \
      -e '/^Requires.private: vulkan$/d' \
      -e 's/[[:space:]]-lvulkan-1//g' \
      "${pc_file}"
  fi
}

build_libplacebo() {
  mkdir -p "${deps_root}" "${prefix}"
  local stamp="${prefix}/.libplacebo-${libplacebo_ref}.stamp"

  if [[ -f "${stamp}" && -f "${prefix}/lib/pkgconfig/libplacebo.pc" ]]; then
    echo "Using existing libplacebo build for ${libplacebo_ref}"
    sanitize_libplacebo_pc
    return
  fi

  if [[ -d "${libplacebo_src}/.git" && ! -f "${libplacebo_src}/meson.build" ]]; then
    echo "Removing interrupted libplacebo checkout at ${libplacebo_src}"
    rm -rf "${libplacebo_src}"
  fi

  if [[ ! -d "${libplacebo_src}/.git" ]]; then
    git clone --depth 1 --branch "${libplacebo_ref}" \
      https://code.videolan.org/videolan/libplacebo.git "${libplacebo_src}"
  else
    git -C "${libplacebo_src}" fetch --depth 1 origin \
      "refs/tags/${libplacebo_ref}:refs/tags/${libplacebo_ref}"
    git -C "${libplacebo_src}" checkout --detach "${libplacebo_ref}^{commit}"
  fi

  meson setup "${libplacebo_build}" "${libplacebo_src}" \
    --wipe \
    --prefix="${prefix}" \
    --buildtype=release \
    --strip \
    --default-library=static \
    --prefer-static \
    -Dvulkan=disabled \
    -Dvk-proc-addr=disabled \
    -Dopengl=disabled \
    -Dgl-proc-addr=disabled \
    -Dd3d11=disabled \
    -Dglslang=disabled \
    -Dshaderc=disabled \
    -Dlcms=disabled \
    -Ddovi=disabled \
    -Dlibdovi=disabled \
    -Ddemos=false \
    -Dtests=false \
    -Dbench=false \
    -Dfuzz=false \
    -Dunwind=disabled \
    -Dxxhash=disabled

  meson compile -C "${libplacebo_build}"
  meson install -C "${libplacebo_build}"
  sanitize_libplacebo_pc
  touch "${stamp}"
}

configure_and_build_app() {
  export PKG_CONFIG_PATH="${prefix}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
  export PKG_CONFIG_ALL_STATIC=1
  export CFLAGS="${CFLAGS:-} -O2"
  export CXXFLAGS="${CXXFLAGS:-} -O2"
  export LDFLAGS="${LDFLAGS:-} -static -static-libgcc -static-libstdc++"

  local build_path="${repo_root}/${builddir}"
  meson setup "${build_path}" "${repo_root}" \
    --wipe \
    --buildtype=release \
    --strip \
    --default-library=static \
    --prefer-static \
    -Dwindows_static=true \
    -Dmpv=enabled \
    -Dsystem_mpv=false \
    -Dlibtorrent=enabled \
    -Dsystem_libtorrent=false \
    -Dthorvg=enabled \
    -Dfreetype=enabled \
    -Dfontconfig=enabled

  meson compile -C "${build_path}"

  local exe="${build_path}/src/torrview.exe"
  if [[ -f "${exe}" ]]; then
    echo
    echo "Built ${exe}"
    echo "Imported DLLs:"
    objdump -p "${exe}" | sed -n '/DLL Name:/p' || true
  fi
}

install_packages
build_ffmpeg
build_libplacebo
configure_and_build_app
