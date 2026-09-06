torrview_android = shared_library(
  'torrview',
  '../../../src/mpv_presentation_android.cpp', '../bridge.cpp',
  dependencies: [torrview_core_dep, avcodec_dep, cpp.find_library('android')],
  include_directories: config_inc,
  # Keep static dependency internals private, including FFmpeg's assembly tables.
  link_args: ['-Wl,--exclude-libs,ALL', '-Wl,-z,max-page-size=16384',
              '-Wl,-z,common-page-size=16384'],
  install: true,
  install_dir: 'lib',
)
