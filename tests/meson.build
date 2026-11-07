cache_test = executable('cache-policy-test', 'cache_policy_test.cpp', '../src/cache_policy.cpp',
  include_directories: include_directories('../src'))
test('cache-policy', cache_test)
if libtorrent_dep.found()
  stream_test = executable('torrent-stream-test', 'torrent_stream_test.cpp',
    dependencies: torrview_core_dep, include_directories: config_inc)
  test('torrent-stream-cancellation', stream_test, timeout: 20)
  disk_test = executable('memory-disk-test', 'memory_disk_test.cpp',
    dependencies: torrview_core_dep, include_directories: config_inc)
  test('memory-disk-accounting', disk_test)
endif

if host_platform != 'android'
  library_test = executable('library-test', 'library_test.cpp', '../src/library.cpp',
    dependencies: [sqlite_dep, torrview_core_dep], include_directories: config_inc)
  test('sqlite-library', library_test)
endif

if host_platform != 'android' and mpv_dep.found()
  resume_test = executable('mpv-resume-test', 'mpv_resume_test.cpp', '../src/library.cpp',
    dependencies: [sqlite_dep, torrview_core_dep], include_directories: config_inc)
  test('mpv-sqlite-resume', resume_test, timeout: 40)
endif
