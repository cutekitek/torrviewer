torrview_sources = files(
  'app.cpp',
  'clay.cpp',
  'input_source.cpp',
  'main.cpp',
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
