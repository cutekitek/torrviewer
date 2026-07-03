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

add_project_arguments(
  cpp.get_supported_arguments([
    '-Wpedantic',
    '-Wconversion',
    '-Wsign-conversion',
    '-Wshadow',
  ]),
  language: 'cpp',
)

if not fs.exists(meson.project_source_root() / 'third_party' / 'clay' / 'clay.h')
  error('Clay submodule is missing. Run: git submodule update --init --recursive')
endif

sdl3_dep = dependency('sdl3', required: true)

if host_machine.system() == 'windows'
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
  mpv_proj = subproject(
    'mpv',
    default_options: [
      'default_library=static',
      'libmpv=true',
      'cplayer=false',
      'tests=false',
      'build-date=false',
      'lua=disabled',
      'javascript=disabled',
      'libavdevice=disabled',
      'vapoursynth=disabled',
      'plain-gl=enabled',
    ],
    required: get_option('mpv'),
  )
  if mpv_proj.found()
    mpv_dep = mpv_proj.get_variable('libmpv_dep')
  endif
endif

libtorrent_dep = dependency('', required: false)
if get_option('libtorrent').allowed()
  libtorrent_dep = dependency('libtorrent-rasterbar', required: false)
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

freetype_dep = dependency('freetype2', required: get_option('freetype'))
fontconfig_dep = dependency('fontconfig', required: get_option('fontconfig'))

thorvg_dep = dependency('thorvg-1', required: false)
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
config.set_quoted('TORRVIEW_FONT_PATH', meson.project_source_root() / 'assets' / 'fonts' / 'NotoSans-Regular.ttf')
config.set_quoted('TORRVIEW_ICON_DIR', meson.project_source_root() / 'assets' / 'icons')
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
