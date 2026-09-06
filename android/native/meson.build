# Android dependency prefixes contain NDK builds, never libraries from the desktop toolchain.
mpv_dep = dependency('mpv', static: true, required: true)
libtorrent_dep = dependency('libtorrent-rasterbar', static: true, required: true)
avcodec_dep = dependency('libavcodec', static: true, required: true)
core_deps = [mpv_dep, libtorrent_dep, cpp.find_library('log'), dependency('threads')]
config = configuration_data()
config.set_quoted('TORRVIEW_VERSION', meson.project_version())
config.set('TORRVIEW_HAVE_MPV', true)
config.set('TORRVIEW_HAVE_LIBTORRENT', true)
config.set('TORRVIEW_TRACY_ALLOCATION_CALLSTACK_DEPTH', 0)
configure_file(input: '../../src/config.hpp.in', output: 'config.hpp', configuration: config)
config_inc = include_directories('.')
