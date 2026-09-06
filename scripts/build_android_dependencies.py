"""Build static Android dependencies on Linux (including WSL) or macOS with NDK r28c.

All downloads and generated files stay in the requested work directory. Existing repository
subprojects are read-only inputs. Native Torrview itself continues to be built by Meson.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import urllib.request

from build_android import ABIS, cross_file, run

SOURCES = {
    "freetype": ("https://github.com/freetype/freetype.git", "VER-2-13-3"),
    "harfbuzz": ("https://github.com/harfbuzz/harfbuzz.git", "10.4.0"),
    "fribidi": ("https://github.com/fribidi/fribidi.git", "v1.0.16"),
    "libass": ("https://github.com/libass/libass.git", "0.17.3"),
    "openssl": ("https://github.com/openssl/openssl.git", "openssl-3.5.0"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work", type=Path, default=Path("build-android-dependencies"))
    parser.add_argument("--abis", default=",".join(ABIS))
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 8))
    parser.add_argument("--boost-headers", type=Path, help="Directory containing boost/; otherwise download Boost 1.87.0")
    args = parser.parse_args()
    if platform.system() not in ("Linux", "Darwin"):
        parser.error("Use Linux/WSL or macOS for dependency builds; the Torrview Meson target also supports Windows.")
    root = Path(__file__).resolve().parents[1]
    work, output, ndk = args.work.resolve(), args.output.resolve(), args.ndk.resolve()
    sources = work / "sources"
    sources.mkdir(parents=True, exist_ok=True)
    receipt = {}
    for name, (url, tag) in SOURCES.items():
        source = sources / name
        if not source.exists():
            run("git", "clone", "--depth=1", "--branch", tag, url, source)
        receipt[name] = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    for name in ("mpv", "ffmpeg", "libplacebo", "libtorrent-rasterbar"):
        source = root / "subprojects" / name
        if not source.is_dir():
            raise SystemExit(f"Missing {source}; initialize repository dependencies as described in docs/build.md.")
        receipt[name] = str(source)
    boost = args.boost_headers
    if boost is None:
        boost = sources / "boost_1_87_0"
        if not boost.exists():
            archive = sources / "boost.tar.gz"
            urllib.request.urlretrieve("https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.gz", archive)
            with tarfile.open(archive) as bundle:
                bundle.extractall(sources, filter="data")
    if not (boost / "boost/version.hpp").is_file():
        raise SystemExit("Boost headers are missing")
    receipt["boost"] = (boost / "boost/version.hpp").read_text()

    for abi in args.abis.split(","):
        if abi not in ABIS:
            parser.error(f"Unsupported ABI {abi}")
        prefix = output / abi
        build = work / abi
        build.mkdir(parents=True, exist_ok=True)
        prefix.mkdir(parents=True, exist_ok=True)
        machine, binary, _ = cross_file(ndk, prefix, abi)
        cross = build / "android.ini"
        cross.write_text(machine, encoding="utf-8")
        triple = ABIS[abi][0]
        env = os.environ.copy()
        env.update({"PATH": str(binary) + os.pathsep + env["PATH"],
                    "ANDROID_NDK_ROOT": str(ndk), "ANDROID_NDK_HOME": str(ndk),
                    "CC": str(binary / f"{triple}26-clang"),
                    "CXX": str(binary / f"{triple}26-clang++"), "AR": str(binary / "llvm-ar"),
                    "RANLIB": str(binary / "llvm-ranlib"), "CFLAGS": "-fPIC -O2",
                    "CXXFLAGS": "-fPIC -O2", "LDFLAGS": "-Wl,-z,max-page-size=16384",
                    "PKG_CONFIG_LIBDIR": str(prefix / "lib/pkgconfig"), "PKG_CONFIG_PATH": ""})
        # NDK system libraries ship headers/stubs, but no pkg-config metadata.
        pc_directory = prefix / "lib/pkgconfig"
        pc_directory.mkdir(parents=True, exist_ok=True)
        for name, library, version in (("egl", "EGL", "1.5"), ("glesv2", "GLESv2", "3.2")):
            (pc_directory / f"{name}.pc").write_text(
                f"Name: {name}\nDescription: Android NDK {library}\nVersion: {version}\nLibs: -l{library}\n",
                encoding="utf-8")

        def meson(name, source, options):
            destination = build / name
            command = ["meson", "setup", str(destination), str(source), "--cross-file", str(cross),
                       "--prefix", str(prefix), "--libdir=lib", "--buildtype=release",
                       "--default-library=static", "-Db_staticpic=true", "--wrap-mode=nofallback"]
            if (destination / "meson-private/coredata.dat").exists():
                command.append("--reconfigure")
            run(*command, *options, env=env)
            run("meson", "compile", "-C", destination, "-j", args.jobs, env=env)
            run("meson", "install", "-C", destination, env=env)

        openssl_build = build / "openssl"
        openssl_build.mkdir(exist_ok=True)
        if not (prefix / "lib/libssl.a").exists():
            target = {"arm64-v8a": "android-arm64", "armeabi-v7a": "android-arm", "x86_64": "android-x86_64"}[abi]
            run("perl", sources / "openssl/Configure", target, "-D__ANDROID_API__=26", "no-shared",
                "no-tests", "no-apps", "--libdir=lib", f"--prefix={prefix}", cwd=openssl_build, env=env)
            run("make", f"-j{args.jobs}", cwd=openssl_build, env=env)
            run("make", "install_sw", cwd=openssl_build, env=env)
        meson("freetype", sources / "freetype", ["-Dharfbuzz=disabled", "-Dpng=disabled", "-Dbzip2=disabled", "-Dbrotli=disabled"])
        meson("fribidi", sources / "fribidi", ["-Ddocs=false", "-Dtests=false", "-Dbin=false"])
        meson("harfbuzz", sources / "harfbuzz", ["-Dauto_features=disabled", "-Dtests=disabled",
              "-Ddocs=disabled", "-Dutilities=disabled"])
        # This libass release uses Autotools. Its build remains isolated from the shared repository.
        libass = sources / "libass"
        if not (libass / "configure").exists():
            run("autoreconf", "-fi", cwd=libass, env=env)
        ass_build = build / "libass"
        ass_build.mkdir(exist_ok=True)
        run(libass / "configure", f"--host={triple}", f"--prefix={prefix}", "--libdir=" + str(prefix / "lib"),
            "--disable-shared", "--enable-static", "--disable-fontconfig", "--disable-require-system-font-provider",
            "--disable-asm", cwd=ass_build, env=env)
        run("make", f"-j{args.jobs}", cwd=ass_build, env=env)
        run("make", "install", cwd=ass_build, env=env)
        # The current FFmpeg Meson port accepts -Djni but leaves CONFIG_JNI=0.
        # Use upstream's Android configuration; do not patch downloaded dependency source.
        ffmpeg_build = build / "ffmpeg-android"
        ffmpeg_build.mkdir(exist_ok=True)
        run(root / "subprojects/ffmpeg/configure", f"--prefix={prefix}", "--libdir=" + str(prefix / "lib"),
            "--target-os=android", "--arch=" + ABIS[abi][1], "--enable-cross-compile", "--enable-pic",
            "--cc=" + env["CC"], "--cxx=" + env["CXX"], "--ar=" + env["AR"], "--ranlib=" + env["RANLIB"],
            "--strip=" + str(binary / "llvm-strip"), "--disable-shared", "--enable-static",
            "--disable-autodetect", "--disable-programs", "--disable-doc", "--disable-network",
            "--disable-devices", "--disable-avdevice", "--disable-encoders", "--disable-muxers",
            "--disable-protocols", "--enable-protocol=file,pipe", "--enable-jni", "--enable-mediacodec",
            "--disable-x86asm", "--extra-cflags=-fPIC", "--extra-ldflags=-Wl,-z,max-page-size=16384",
            cwd=ffmpeg_build, env=env)
        ffmpeg_config = (ffmpeg_build / "config.h").read_text()
        for feature in ("CONFIG_JNI", "CONFIG_MEDIACODEC"):
            if f"#define {feature} 1" not in ffmpeg_config:
                raise SystemExit(f"FFmpeg Android configuration did not enable {feature}")
        run("make", f"-j{args.jobs}", cwd=ffmpeg_build, env=env)
        run("make", "install", cwd=ffmpeg_build, env=env)
        meson("libplacebo", root / "subprojects/libplacebo", ["-Dauto_features=disabled",
              "-Dopengl=enabled", "-Dgl-proc-addr=enabled", "-Ddemos=false", "-Dtests=false"])
        meson("mpv", root / "subprojects/mpv", ["-Dauto_features=disabled", "-Dlibmpv=true", "-Dcplayer=false",
              "-Dtests=false", "-Dgl=enabled", "-Degl=enabled", "-Degl-android=enabled",
              "-Daudiotrack=enabled", "-Dopensles=enabled", "-Dandroid-media-ndk=enabled"])
        # Boost is header-only for libtorrent >= 2 with Boost >= 1.69.
        installed_version = prefix / "include/boost/version.hpp"
        if not installed_version.exists() or installed_version.read_bytes() != (boost / "boost/version.hpp").read_bytes():
            installed_boost = prefix / "include/boost"
            if installed_boost.exists():
                if not installed_boost.resolve().is_relative_to(output):
                    raise SystemExit("Boost install path escaped the dependency output directory")
                shutil.rmtree(installed_boost)
            shutil.copytree(boost / "boost", installed_boost, copy_function=shutil.copy)
        torrent_build = build / "libtorrent"
        boost_stamp = torrent_build / "torrview-boost-version"
        boost_version = (boost / "boost/version.hpp").read_bytes()
        clean_torrent = not boost_stamp.exists() or boost_stamp.read_bytes() != boost_version
        run("cmake", "-S", root / "subprojects/libtorrent-rasterbar", "-B", torrent_build, "-G", "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build/cmake/android.toolchain.cmake'}", f"-DANDROID_ABI={abi}",
            "-DANDROID_PLATFORM=android-26", "-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_CXX_STANDARD=20",
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DBUILD_SHARED_LIBS=OFF", "-Dbuild_tests=OFF",
            "-Dbuild_examples=OFF", "-Dbuild_tools=OFF", "-Dpython-bindings=OFF",
            f"-DCMAKE_INSTALL_PREFIX={prefix}", "-DCMAKE_INSTALL_LIBDIR=lib", f"-DCMAKE_PREFIX_PATH={prefix}",
            f"-DOPENSSL_ROOT_DIR={prefix}", "-DOPENSSL_USE_STATIC_LIBS=TRUE", f"-DBOOST_ROOT={prefix}",
            f"-DBoost_INCLUDE_DIR={prefix / 'include'}",
            "-DBoost_NO_BOOST_CMAKE=ON", "-DCMAKE_POLICY_DEFAULT_CMP0167=OLD", env=env)
        run("cmake", "--build", torrent_build, "--parallel", args.jobs,
            *(["--clean-first"] if clean_torrent else []), env=env)
        run("cmake", "--install", torrent_build, env=env)
        boost_stamp.write_bytes(boost_version)
        (prefix / "build-receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
