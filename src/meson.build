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
  torrview_sources,
  dependencies: torrview_deps,
  include_directories: config_inc,
  install: true,
)
