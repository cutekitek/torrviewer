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

mpv_dep = dependency('mpv', required: get_option('mpv'))
libtorrent_dep = dependency('libtorrent-rasterbar', required: get_option('libtorrent'))
freetype_dep = dependency('freetype2', required: get_option('freetype'))
fontconfig_dep = dependency('fontconfig', required: get_option('fontconfig'))

clay_dep = declare_dependency(
  include_directories: include_directories('third_party/clay', is_system: true),
)

torrview_deps = [
  sdl3_dep,
  opengl_dep,
  clay_dep,
]

foreach dep : [mpv_dep, libtorrent_dep, freetype_dep, fontconfig_dep]
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

configure_file(
  input: 'src/config.hpp.in',
  output: 'config.hpp',
  configuration: config,
)

config_inc = include_directories('.')

subdir('src')

summary({
  'SDL3': sdl3_dep.found(),
  'OpenGL': opengl_dep.found(),
  'Clay': true,
  'libmpv': mpv_dep.found(),
  'libtorrent-rasterbar': libtorrent_dep.found(),
  'FreeType': freetype_dep.found(),
  'Fontconfig': fontconfig_dep.found(),
}, section: 'Dependencies')
