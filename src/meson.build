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
  'app_settings.cpp',
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
  'ui/settings_page.cpp',
  'ui/title_page.cpp',
)

windows_host = host_machine.system() == 'windows'
production_win_subsystem = windows_host ? 'windows' : 'console'
torrview_app_sources = torrview_sources + packed_resources
if tracy_dep.found() and get_option('tracy_allocations')
  torrview_app_sources += files('tracy_allocations.cpp')
endif

executable(
  'torrview',
  torrview_app_sources,
  dependencies: torrview_deps,
  include_directories: config_inc,
  win_subsystem: production_win_subsystem,
  install: true,
)

if windows_host
  executable(
    'torrview-dev',
    torrview_app_sources,
    dependencies: torrview_deps,
    include_directories: config_inc,
    win_subsystem: 'console',
    build_by_default: false,
    install: false,
  )
endif
