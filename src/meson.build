python = import('python').find_installation()

packed_resources = custom_target(
  'packed_resources',
  input: resource_files,
  output: ['resources.cpp', 'resources.hpp'],
  command: [
    python,
    meson.project_source_root() / 'scripts' / 'pack_resources.py',
    '--prefix', meson.project_source_root() / 'assets',
    '--source', '@OUTPUT0@',
    '--header', '@OUTPUT1@',
    '--namespace', 'torrview::resources',
    '@INPUT@',
  ],
  depend_files: resource_packer,
)

torrview_sources = files(
  'app.cpp',
  'cache_policy.cpp',
  'clay.cpp',
  'input_source.cpp',
  'logger.cpp',
  'main.cpp',
  'memory_disk_io.cpp',
  'mpv_player.cpp',
  'sdl_event_manager.cpp',
  'torrent_service.cpp',
  'ui/clay_renderer.cpp',
  'ui/file_browser_page.cpp',
  'ui/player_overlay.cpp',
  'ui/title_page.cpp',
)

executable(
  'torrview',
  torrview_sources + packed_resources,
  dependencies: torrview_deps,
  include_directories: config_inc,
  install: true,
)
