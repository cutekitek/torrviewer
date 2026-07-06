project(
  'torrview',
  'cpp',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'cpp_std=c++20',
    'warning_level=3',
    'werror=false',
  ],
)

fs = import('fs')
cpp = meson.get_compiler('cpp')
windows_static = get_option('windows_static')
static_linkage = windows_static

add_project_arguments(
  cpp.get_supported_arguments([
    '-Wpedantic',
    '-Wconversion',
    '-Wsign-conversion',
    '-Wshadow',
  ]),
  language: 'cpp',
)

if windows_static and host_machine.system() != 'windows'
  error('windows_static=true is only supported for Windows targets')
endif

if windows_static
  add_project_link_arguments(
    cpp.get_supported_link_arguments([
      '-static',
      '-static-libgcc',
      '-static-libstdc++',
    ]),
    language: 'cpp',
  )
endif

if not fs.exists(meson.project_source_root() / 'third_party' / 'clay' / 'clay.h')
  error('Clay submodule is missing. Run: git submodule update --init --recursive')
endif

sdl3_dep = dependency('sdl3', static: static_linkage, required: true)

if host_machine.system() == 'windows'
  add_project_arguments('-DNOMINMAX', language: 'cpp')
  opengl_dep = cpp.find_library('opengl32', required: true)
else
  opengl_dep = dependency('opengl', required: false)
  if not opengl_dep.found()
    opengl_dep = dependency('gl', required: false)
  endif
  if not opengl_dep.found()
    opengl_dep = cpp.find_library('GL', required: true)
  endif
endif

mpv_dep = dependency('', required: false)
if get_option('mpv').allowed()
  if get_option('system_mpv') and not windows_static
    mpv_dep = dependency('mpv', static: static_linkage, required: false)
  endif
  if not mpv_dep.found()
    mpv_proj = subproject(
      'mpv',
      default_options: [
        'default_library=static',
        'libmpv=true',
        'cplayer=false',
        'tests=false',
        'fuzzers=false',
        'build-date=false',
        'html-build=disabled',
        'manpage-build=disabled',
        'pdf-build=disabled',
        'caca=disabled',
        'cdda=disabled',
        'cplugins=disabled',
        'dvbin=disabled',
        'dvdnav=disabled',
        'jpeg=disabled',
        'lcms2=disabled',
        'libarchive=disabled',
        'lua=disabled',
        'javascript=disabled',
        'libbluray=disabled',
        'libavdevice=disabled',
        'rubberband=disabled',
        'shaderc=disabled',
        'spirv-cross=disabled',
        'uchardet=disabled',
        'vapoursynth=disabled',
        'zimg=disabled',
        'alsa=disabled',
        'audiounit=disabled',
        'avfoundation=disabled',
        'coreaudio=disabled',
        'jack=disabled',
        'openal=disabled',
        'audiotrack=disabled',
        'aaudio=disabled',
        'opensles=disabled',
        'oss-audio=disabled',
        'pipewire=disabled',
        'pulse=disabled',
        'sdl2-audio=disabled',
        'sndio=disabled',
        'wasapi=enabled',
        'cocoa=disabled',
        'd3d11=disabled',
        'direct3d=disabled',
        'dmabuf-wayland=disabled',
        'drm=disabled',
        'egl=disabled',
        'egl-angle=disabled',
        'egl-angle-lib=disabled',
        'egl-angle-win32=disabled',
        'gbm=disabled',
        'gl=enabled',
        'gl-cocoa=disabled',
        'gl-dxinterop=disabled',
        'gl-win32=disabled',
        'gl-x11=disabled',
        'plain-gl=enabled',
        'sdl2-video=disabled',
        'sixel=disabled',
        'vaapi=disabled',
        'vulkan=disabled',
        'wayland=disabled',
        'x11=disabled',
        'xv=disabled',
        'android-media-ndk=disabled',
        'cuda-hwaccel=disabled',
        'cuda-interop=disabled',
        'd3d-hwaccel=enabled',
        'd3d9-hwaccel=enabled',
        'gl-dxinterop-d3d9=disabled',
        'videotoolbox-gl=disabled',
        'videotoolbox-pl=disabled',
        'win32-smtc=disabled',
      ],
      required: get_option('mpv'),
    )
    if mpv_proj.found()
      mpv_dep = mpv_proj.get_variable('libmpv_dep')
    endif
  endif
endif

libtorrent_dep = dependency('', required: false)
if get_option('libtorrent').allowed()
  if get_option('system_libtorrent') and not windows_static
    libtorrent_dep = dependency('libtorrent-rasterbar', static: static_linkage, required: false)
  endif
  if not libtorrent_dep.found()
    cmake = import('cmake')
    libtorrent_options = cmake.subproject_options()
    libtorrent_options.add_cmake_defines({
      'BUILD_SHARED_LIBS': false,
      'build_tests': false,
      'build_examples': false,
      'build_tools': false,
      'python-bindings': false,
      'python-egg-info': false,
      'developer-options': false,
      'dht': true,
      'extensions': true,
      'streaming': true,
    })
    if windows_static
      libtorrent_options.add_cmake_defines({
        'Boost_USE_STATIC_LIBS': true,
        'OPENSSL_USE_STATIC_LIBS': true,
      })
    endif
    libtorrent_proj = cmake.subproject(
      'libtorrent-rasterbar',
      options: libtorrent_options,
      required: get_option('libtorrent'),
    )
    if libtorrent_proj.found()
      libtorrent_dep = libtorrent_proj.dependency('torrent-rasterbar')
    endif
  endif
endif

freetype_dep = dependency('freetype2', static: static_linkage, required: get_option('freetype'))
fontconfig_dep = dependency('fontconfig', static: static_linkage, required: get_option('fontconfig'))

thorvg_dep = dependency('', required: false)
if not windows_static
  thorvg_dep = dependency('thorvg-1', static: static_linkage, required: false)
endif
if not thorvg_dep.found() and get_option('thorvg').allowed()
  thorvg_proj = subproject(
    'thorvg',
    default_options: [
      'default_library=static',
      'engines=gl',
      'loaders=svg,ttf',
      'savers=',
      'tools=',
      'tests=false',
      'bindings=',
      'threads=false',
      'simd=false',
      'extra=',
      'static=true',
      'file=true',
    ],
    required: get_option('thorvg'),
  )
  if thorvg_proj.found()
    thorvg_dep = thorvg_proj.get_variable('thorvg_dep')
  endif
endif

clay_dep = declare_dependency(
  include_directories: include_directories('third_party/clay', is_system: true),
)

resource_files = files(
  'assets/fonts/NotoSans-Regular.ttf',
  'assets/icons/audio-next.svg',
  'assets/icons/audio-prev.svg',
  'assets/icons/audio.svg',
  'assets/icons/close.svg',
  'assets/icons/forward.svg',
  'assets/icons/gear.svg',
  'assets/icons/fullscreen.svg',
  'assets/icons/pause.svg',
  'assets/icons/play.svg',
  'assets/icons/rewind.svg',
  'assets/icons/volume.svg',
  'assets/icons/window.svg',
)
resource_packer = files('scripts/pack_resources.py')

torrview_deps = [
  sdl3_dep,
  opengl_dep,
  clay_dep,
]

foreach dep : [mpv_dep, libtorrent_dep, freetype_dep, fontconfig_dep, thorvg_dep]
  if dep.found()
    torrview_deps += dep
  endif
endforeach

config = configuration_data()
config.set_quoted('TORRVIEW_VERSION', meson.project_version())
config.set('TORRVIEW_HAVE_MPV', mpv_dep.found())
config.set('TORRVIEW_HAVE_LIBTORRENT', libtorrent_dep.found())
config.set('TORRVIEW_HAVE_FREETYPE', freetype_dep.found())
config.set('TORRVIEW_HAVE_FONTCONFIG', fontconfig_dep.found())
config.set('TORRVIEW_HAVE_THORVG', thorvg_dep.found())

configure_file(
  input: 'src/config.hpp.in',
  output: 'config.hpp',
  configuration: config,
)

config_inc = include_directories('.')

subdir('src')

if libtorrent_dep.found()
  memory_disk_io_test = executable(
    'memory_disk_io_test',
    files('tests/memory_disk_io_test.cpp', 'src/cache_policy.cpp', 'src/memory_disk_io.cpp'),
    dependencies: libtorrent_dep,
    include_directories: [config_inc, include_directories('src')],
  )
  test('memory_disk_io', memory_disk_io_test)

  torrent_stream_probe = executable(
    'torrent_stream_probe',
    files(
      'tests/torrent_stream_probe.cpp',
      'src/cache_policy.cpp',
      'src/logger.cpp',
      'src/memory_disk_io.cpp',
    ),
    dependencies: libtorrent_dep,
    include_directories: [config_inc, include_directories('src')],
  )
  test('torrent_stream_probe_skip_without_input', torrent_stream_probe)
endif

summary({
  'SDL3': sdl3_dep.found(),
  'OpenGL': opengl_dep.found(),
  'Clay': true,
  'libmpv': mpv_dep.found(),
  'libtorrent-rasterbar': libtorrent_dep.found(),
  'FreeType': freetype_dep.found(),
  'Fontconfig': fontconfig_dep.found(),
  'ThorVG': thorvg_dep.found(),
}, section: 'Dependencies')
