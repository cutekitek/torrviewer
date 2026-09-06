# This source list is shared by desktop and Android; presentation is supplied by the host.
torrview_core = static_library(
  'torrview-core',
  '../cache_policy.cpp', '../input_source.cpp', '../logger.cpp',
  '../memory_disk_io.cpp', '../mpv_player.cpp', '../torrent_service.cpp',
  dependencies: core_deps,
  include_directories: config_inc,
  pic: host_platform == 'android',
)
torrview_core_dep = declare_dependency(
  link_with: torrview_core,
  dependencies: core_deps,
  include_directories: include_directories('..'),
)
