# Android builds

Torrview's Android application uses Kotlin/Jetpack Compose, with separate phone/tablet and
Compose for TV controls. It supports Android 8 (API 26) and later. The native library shares
the desktop torrent/cache/player sources through `src/core/meson.build`.

## Toolchain

- Android SDK Platform 35 and Build Tools 35.0.0; NDK **28.2.13676358 (r28c)**.
- JDK 17 or 21. The checked-in Gradle wrapper uses 8.11.1 and AGP 8.9.2.
- Python 3.12+, Meson 1.4+, Ninja, pkg-config, Git, CMake, Make, Perl, Autoconf,
  Automake, and Libtool for native dependency builds; Python Mako and Jinja2.
- Initialize the existing native dependency sources as described in `build.md`.
  Dependency builds require Linux (WSL is supported by Torrview's script) or macOS.
  Use a Linux-native work directory under WSL for build speed.

Install Python tooling in a virtual environment and put its `bin` directory on PATH:

```sh
python3 -m venv .cache/android-tools
. .cache/android-tools/bin/activate
pip install meson mako jinja2
```

## Native dependencies

Build the static dependency prefixes once per NDK/ABI/dependency revision:

```sh
python scripts/build_android_dependencies.py \
  --ndk "$ANDROID_HOME/ndk/28.2.13676358" \
  --output "$PWD/android/build/dependencies" \
  --work "$PWD/build-android-dependencies" \
  --abis arm64-v8a,armeabi-v7a,x86_64
```

The script builds the repository's FFmpeg, libplacebo, mpv, and libtorrent sources,
plus pinned supporting libraries. It downloads Boost 1.87.0 headers unless
`--boost-headers /path/to/include` supplies a directory containing `boost/`.
Downloaded sources and generated builds are outside the first-party source tree;
existing subproject sources are not modified. Each prefix includes a build receipt.

The Android dependency configuration enables EGL, Android MediaCodec and Android audio,
and disables FFmpeg programs, encoders, muxers, and network protocols. All libraries
are static/PIC; only the final `libtorrview.so` is packaged. Its C++ runtime is linked
statically, avoiding multiple C++ runtimes. Rebuild prefixes when dependency options
or source versions change. Retain dependency license notices when distributing builds.

## Build the app

```sh
cd android
./gradlew :app:assembleDebug :app:testDebugUnitTest :app:lintDebug \
  -PnativeDeps="$PWD/build/dependencies"
```

Gradle invokes `scripts/build_android.py`, which creates Meson NDK cross files,
builds the JNI target, checks its ABI and 16 KiB ELF LOAD alignment, and rejects
unpackaged non-system shared-library dependencies. The packaged copy is stripped;
the original Meson output retains native symbols for debugging. Outputs are generated under
`build-android/<abi>` and `android/app/build`; never commit them.

For a quicker emulator build, add `-Pabis=x86_64`. For a phone build, add
`-Pabis=arm64-v8a`. The default builds separate APKs for ARM32, ARM64, and x86_64,
with no universal APK. Debug outputs are in `android/app/build/outputs/apk/debug/`:

- `app-arm64-v8a-debug.apk` — ARM64 phones and TVs.
- `app-armeabi-v7a-debug.apk` — ARM32 devices.
- `app-x86_64-debug.apk` — x86_64 devices and emulators.

Each APK installs independently and contains only its architecture's native libraries.
Release builds use the same ABI split configuration.

On Windows, set `JAVA_HOME` to Android Studio's `jbr` directory if the system JDK
is newer than Gradle supports. Use `gradlew.bat` and a consistent toolchain for
native compilation. Windows can consume relocatable ABI prefixes built on Linux,
but needs the **Windows** NDK to compile the JNI library locally.

Alternatively, let Windows Gradle run the native build through WSL:

```powershell
./gradlew.bat :app:assembleDebug `
  -PnativeHost=wsl -PwslDistribution=Ubuntu `
  -PnativeNdk=/home/me/Android/Sdk/ndk/28.2.13676358 `
  -PnativeDeps=/home/me/torrview-dependencies `
  -Ppython=/home/me/android-tools/bin/python `
  -PnativePath=/home/me/android-tools/bin:/usr/bin:/bin
```

Substitute the installed distribution and actual Linux paths. Run from a repository
location accessible to WSL. Native code is still compiled with Meson/Ninja. No
CMake build definition for Torrview is introduced; CMake is used only by libtorrent.
FFmpeg and libass use their upstream configure/Make builds; FFmpeg's current Meson port does not enable Android JNI correctly.

For Kotlin-only development, `-x :app:buildNative` skips native compilation. An APK
built this way is usable only if matching native libraries were already generated.

## Production builds and signing

`assembleDebug` produces debuggable APKs signed with Android's development key.
`assembleRelease` produces non-debuggable APKs with R8 code optimization/obfuscation
and resource shrinking. Both variants use the same optimized, stripped native engine;
the native Meson build already uses `--buildtype=release`.

Create a dedicated release key with JDK `keytool`, or use your existing key. Keep the
key and a Java properties file outside the repository. By default, Gradle loads
`~/.android/torrview/release-signing.properties`:

```properties
storeFile=release.p12
storePassword=YOUR_KEYSTORE_PASSWORD
keyAlias=torrview
keyPassword=YOUR_KEY_PASSWORD
```

`storeFile` is resolved relative to the properties file. Override its location with
`-PreleaseSigningProperties=/absolute/path/release-signing.properties`. Protect this
directory and back up both files securely: future updates must use the same signing
key. Do not put credentials in Git or command-line Gradle password arguments.

```sh
cd android
./gradlew :app:assembleRelease :app:testReleaseUnitTest :app:lintRelease \
  -PnativeDeps="$PWD/build/dependencies"
```

On Windows/WSL, use the same native toolchain arguments shown above, changing the
tasks to `:app:assembleRelease :app:testReleaseUnitTest :app:lintRelease`.
Signed outputs are in `android/app/build/outputs/apk/release/`:

- `app-arm64-v8a-release.apk`
- `app-armeabi-v7a-release.apk`
- `app-x86_64-release.apk`

Without a signing properties file, release builds remain unsigned and are named
`*-release-unsigned.apk`; they cannot be installed until signed. Release builds never
fall back to the debug signing key. `:app:bundleRelease` uses the same signing setup
when an Android App Bundle is needed.

A release APK signed with a new key cannot update an existing debug-signed install
with the same application ID. Remove the debug app first (which clears its app data),
then install release; future release updates can install over it using the same key.
Keep the generated `app/build/outputs/mapping/release/mapping.txt` alongside each
distributed release for crash deobfuscation.

## Runtime architecture

`NativeEngine` owns one serialized worker. JNI creation, commands, event/alert pumping,
state copying, surface replacement, and teardown run on this worker. Snapshots are
published to StateFlow at 4 Hz; native events are serviced at 20 Hz. The Compose UI
never reads mutable C++ state. Torrent stream callbacks run on libmpv threads, using
the existing stream synchronization and cancellation boundary.

The player is destroyed before its torrent provider. JNI owns a duplicated local
file descriptor until playback stops and owns Surface global references until mpv
detaches. Failed surface operations retain references until safe teardown.
Surface callbacks carry identity and dimensions; disposing an old view cannot detach
its replacement. A transient `null` output fallback keeps media open if Android
retires a BufferQueue during GPU initialization. The next surface restores GPU output.
Surface dimensions follow mpv's [Android embedding interface](https://mpv.io/manual/stable/#options-android-surface-size).
Torrent cache access is scoped by session and info hash, including during overlapping
engine shutdown/startup. Snapshot copies and stream startup are serialized.
Android supplies private metadata/font paths and exports the platform default CA
roots for OpenSSL tracker verification. Native logging uses Logcat's `Torrview` tag.

The ViewModel survives rotation. Leaving the foreground pauses playback and suspends
torrent transfers; returning does not automatically resume playback. Audio focus loss
and unplugging headphones pause playback. No background service or PiP is included.
Remote media buttons use Android's MediaSession. Process recreation offers **Resume
previous**, restoring the source, selected torrent file, and position, paused.
The RAM torrent payload cache is rebuilt; it is never restored from a payload file.

Default torrent cache: 128 MiB (64 MiB on low-RAM devices). User limits are 16–512 MiB;
memory pressure reduces the live limit to at most 64 MiB. The libmpv demuxer additionally
budgets 16 MiB forward and 4 MiB backward; decoder/GPU memory is separate.

Documents use ContentResolver. Only torrent metadata (maximum 16 MiB) is copied;
local media uses a seekable descriptor. Non-seekable document providers are rejected.
On TVs without a document picker, magnet entry remains available.

## Validation

```sh
meson test -C build --print-errorlogs
cd android
./gradlew :app:testDebugUnitTest :app:lintDebug
./gradlew :app:connectedDebugAndroidTest -Pabis=x86_64
adb install -r app/build/outputs/apk/debug/app-x86_64-debug.apk
adb logcat -s Torrview AndroidRuntime
```

The native tests cover cache bounds, overlapping writes/accounting, eviction and
retained pieces, EOF/bounds, cancelled blocked reads, and reset during a blocked read.
The player uses translucent top and bottom controls with 48 dp touch/focus targets.
Tap the video to hide or show them. Light segments on the seek bar represent mpv's
buffered, seekable time ranges; teal represents playback progress. Torrent download
speed, peer count, and cache usage are available through the top debug button.
The seekable player cache remains bounded to 16 MiB forward and 4 MiB backward,
separate from the torrent payload cache.

Android unit tests cover snapshot decoding and settings bounds. Instrumentation tests
cover descriptor ownership, non-seekable providers, repeated open/seek/stop/release,
malformed media/metadata errors, Activity and video-surface recreation, and foreground
transitions, plus audio/subtitle selection during playback. A private test peer serves
the generated video fixture with BEP 9 metadata:
the magnet test exercises peer discovery, file selection, RAM caching, the custom mpv
stream, seeking, and stop without relying on a public swarm. Another test cancels
mpv while it waits for a torrent with no peers.

Before releasing, test physical ARM64 phones and ARM32/ARM64 TV devices: local video,
magnets, torrent-file selection, seeks beyond the cached window, subtitles/audio tracks,
network loss/reconnection, rotation/surface recreation, audio interruptions, background
return, process death, memory pressure, and every TV flow with only a remote.
An emulator does not establish physical MediaCodec compatibility.

Validate APK ZIP alignment with SDK Build Tools:

```sh
zipalign -c -P 16 -v 4 app/build/outputs/apk/debug/app-arm64-v8a-debug.apk
```

Repeat the alignment check for each generated APK.

Also run on a 16 KiB Android image/device. ELF and ZIP alignment alone cannot test
all runtime assumptions inside native dependencies.
