# Android milestone validation

Validated on 2026-09-05. This is a development APK for device testing, not a
production-release certification. Build instructions are in [android.md](android.md).

## Completed

- Windows desktop `dev` compiled with MSYS2 UCRT64. Startup, file selection, and
  SDL/OpenGL local video playback were smoke-tested.
- Windows desktop `prod` compiled with MSYS2 Clang64 and its bundled static dependencies.
- A desktop development build with both mpv and libtorrent disabled compiled;
  its cache-policy test passed.
- Three first-party native tests passed: cache bounds, disk byte accounting/eviction
  and isolation between sessions/torrents, and stream bounds/cancellation/reset while blocked.
- JNI compiled with NDK r28c for `arm64-v8a`, `armeabi-v7a`, and `x86_64`.
- Two Kotlin unit tests and Android lint passed (lint has warnings, no errors).
- Seven instrumentation tests passed on an Android 16/API 36 x86_64 emulator:
  borrowed descriptor ownership and repeated teardown; rejection of non-seekable
  providers; three consecutive Activity/surface recreations with rendered video;
  background/foreground pause behavior; malformed media/torrent errors; audio and
  subtitle switching during playback; magnet metadata and video streaming from a
  private test peer; and cancellation of a torrent without peers while mpv waits for data.
  Some tests cover more than one of these behaviors.
- All six packaged native libraries (Torrview and AndroidX graphics, across the three
  ABIs) passed ELF LOAD alignment checks. SDK `zipalign -c -P 16 -v 4` and APK signature
  verification passed. The debug APK installed and launched on the emulator.

The generated video fixtures contain only synthetic blue frames, tones, and a subtitle.
The BitTorrent integration fixture runs its own loopback peer and needs no public swarm.
The emulator uses a 4 KiB kernel; alignment checks do not establish 16 KiB runtime behavior.

## Still required before release

- Physical ARM64 phone and ARM32/ARM64 TV testing, including MediaCodec hardware
  decoding, audible sound, high-bitrate media, and complete D-pad navigation.
- Android 8 and a 16 KiB page-size device/image; the emulator above validates neither.
- Real document providers and TVs without pickers; persisted document permissions and
  process-death recovery; audio focus/headphone interruptions; low-memory handling;
  network loss/reconnection and real tracker/peer behavior.
- Long media seeks beyond the cache window on Android. Native policy tests cover
  eviction bounds, but the small integration video fits in the cache.
- Desktop playback from a live torrent after extraction; the automated torrent tests
  and Android loopback playback cover the shared stream path, not that manual GUI flow.
- Release signing, dependency-license distribution requirements, and store packaging.

The app, engine, UI/lifecycle handling, build scripts, and APK are implemented. The
remaining items above are the physical-device and release acceptance work.

## Per-architecture packaging — 2026-09-06

Gradle `:app:assembleDebug` now produces three independent APKs and no universal APK:
ARM64 (36.9 MiB), ARM32 (33.0 MiB), and x86_64 (39.5 MiB). Each archive was checked
to contain only the expected ABI, including `libtorrview.so`. ELF 16 KiB alignment,
SDK ZIP alignment, and APK signature verification passed for all three outputs.
See [android.md](android.md) for filenames and build commands. This packaging change
does not change the runtime/device coverage recorded above.

## Compact Android player overlay — 2026-09-06

- Replaced the large playback panel with translucent top/bottom controls, small icons
  and 48 dp touch/focus targets. Download details now live in a button-opened debug
  dialog. Local document display names are resolved off the UI thread.
- The timeline displays mpv's seekable demuxer time ranges, including disjoint ranges.
  Android explicitly enables the bounded player cache because `cache=auto` does not
  enable seekable caching for the custom torrent protocol. Existing 16 MiB forward /
  4 MiB backward limits remain; torrent payload storage remains memory-backed.
- All three Android native builds and APK builds passed. Two Kotlin tests, eight
  emulator instrumentation tests, and lint passed (no lint errors). The torrent test
  checks real buffered ranges and clearing on stop; the UI test checks seeking and
  live statistics visible only after opening debug. Document filenames are checked
  in the playback integration test.
- Manually opened a local video through DocumentsUI and inspected portrait and
  landscape playback, seek-bar buffering, debug opening/dismissal, hiding controls,
  and D-pad control reveal/focus on the API 36 x86_64 emulator.
- Desktop `dev`, `prod`, and feature-disabled `dev` compiled. The three first-party
  native Meson tests passed. Existing production warnings remain. Desktop GUI smoke
  testing was not repeated for this overlay change.
- Final separate APKs: ARM64 37.0 MiB, ARM32 33.0 MiB, x86_64 39.6 MiB. Each passed
  ABI-content, ELF alignment, ZIP alignment, and signature checks. The final x86_64
  APK installed and launched successfully.

Physical phone/TV and 16 KiB runtime coverage remain as listed above; the small
integration fixture does not validate long-media cache eviction on Android.

## Signed production APKs — 2026-09-06

- Configured a dedicated release signing key outside the repository, with optional
  `releaseSigningProperties` override. No passwords or private keys are checked in.
- Release disables debugging and enables R8 optimization/obfuscation and resource
  shrinking. Native binaries already use the optimized Meson release configuration.
- `:app:assembleRelease :app:testReleaseUnitTest :app:lintRelease` passed with the
  full native WSL toolchain for all three ABIs. Both Kotlin unit tests passed;
  release lint completed without errors.
- Signed ARM64, ARM32, and x86_64 APKs are 28.8, 24.8, and 31.4 MiB respectively.
  All passed expected-ABI contents, native ELF 16 KiB LOAD alignment, ZIP 16 KiB
  alignment, and signature verification. All use the same release certificate,
  distinct from debug, and none have the application debuggable flag.
- The signed, minified x86_64 release installed on the disposable API 36 emulator
  after removing its debug copy. Startup, document-picker local video playback,
  pause, seeking, portrait/landscape surface recreation, and the subtitle menu were
  smoke-tested. The displayed video and buffered timeline were visually verified.
- The eight instrumentation tests above were run against debug, not this minified
  release. Physical ARM/TV and 16 KiB runtime coverage remains outstanding.
