"""Build Torrview's Meson JNI target using ABI-specific NDK dependency prefixes."""
import argparse
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess

ABIS = {
    "arm64-v8a": ("aarch64-linux-android", "aarch64", "aarch64", 183),
    "armeabi-v7a": ("armv7a-linux-androideabi", "arm", "armv7", 40),
    "x86_64": ("x86_64-linux-android", "x86_64", "x86_64", 62),
}
SYSTEM_LIBS = {"libc.so", "libm.so", "libdl.so", "liblog.so", "libandroid.so", "libz.so",
               "libEGL.so", "libGLESv2.so", "libGLESv3.so", "libOpenSLES.so", "libmediandk.so",
               "libaaudio.so", "libvulkan.so"}


def run(*args, **kwargs):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def quote(value):
    return "'" + str(value).replace("\\", "/").replace("'", "\\'") + "'"


def cross_file(ndk, prefix, abi):
    host = {"Windows": "windows-x86_64", "Linux": "linux-x86_64", "Darwin": "darwin-x86_64"}[platform.system()]
    binary = ndk / "toolchains/llvm/prebuilt" / host / "bin"
    suffix = ".exe" if platform.system() == "Windows" else ""
    if not (binary / ("clang" + suffix)).is_file():
        raise SystemExit(f"NDK compiler not found under {binary}. Install NDK 28.2.13676358.")
    triple, family, cpu, _ = ABIS[abi]
    pkgconfig = shutil.which("pkg-config")
    if not pkgconfig:
        raise SystemExit("pkg-config is required on PATH")
    # Overriding prefix also permits using a dependency prefix built on a different host OS.
    text = f"""[binaries]
c = [{quote(binary / ('clang' + suffix))}, '--target={triple}26']
cpp = [{quote(binary / ('clang++' + suffix))}, '--target={triple}26']
ar = {quote(binary / ('llvm-ar' + suffix))}
strip = {quote(binary / ('llvm-strip' + suffix))}
pkg-config = [{quote(pkgconfig)}, {quote('--define-variable=prefix=' + prefix.as_posix())}]
[host_machine]
system = 'android'
cpu_family = '{family}'
cpu = '{cpu}'
endian = 'little'
[properties]
needs_exe_wrapper = true
pkg_config_libdir = [{quote(prefix / 'lib/pkgconfig')}, {quote(prefix / 'share/pkgconfig')}]
[built-in options]
c_args = ['-fPIC']
cpp_args = ['-fPIC']
c_link_args = ['-Wl,-z,max-page-size=16384']
cpp_link_args = ['-static-libstdc++', '-Wl,-z,max-page-size=16384']
"""
    return text, binary, suffix


def verify_elf(path, abi):
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[5] != 1:
        raise ValueError(f"Not a little-endian ELF library: {path}")
    if struct.unpack_from("<H", data, 18)[0] != ABIS[abi][3]:
        raise ValueError(f"Wrong ELF architecture for {abi}: {path}")
    bits64 = data[4] == 2
    offset = struct.unpack_from("<Q" if bits64 else "<I", data, 32 if bits64 else 28)[0]
    entry_size, count = struct.unpack_from("<HH", data, 54 if bits64 else 42)
    load_count = 0
    for index in range(count):
        start = offset + index * entry_size
        kind = struct.unpack_from("<I", data, start)[0]
        alignment = struct.unpack_from("<Q" if bits64 else "<I", data, start + (48 if bits64 else 28))[0]
        if kind == 1:
            load_count += 1
            if alignment < 16384:
                raise ValueError(f"ELF LOAD segment is not 16 KiB aligned: {path}")
    if not load_count:
        raise ValueError(f"No ELF LOAD segments: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--deps", type=Path, required=True)
    parser.add_argument("--abis", default=",".join(ABIS))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    for abi in args.abis.split(","):
        if abi not in ABIS:
            parser.error(f"Unsupported ABI: {abi}")
        prefix = (args.deps / abi).resolve()
        for dependency in ("mpv", "libtorrent-rasterbar", "libavcodec"):
            if not (prefix / "lib/pkgconfig" / f"{dependency}.pc").is_file():
                raise SystemExit(f"Missing Android {dependency} for {abi} in {prefix}. "
                                 "Run scripts/build_android_dependencies.py; see docs/android.md.")
        build = root / "build-android" / abi
        build.mkdir(parents=True, exist_ok=True)
        machine, binary, suffix = cross_file(args.ndk.resolve(), prefix, abi)
        cross = build / "android.ini"
        if not cross.exists() or cross.read_text() != machine:
            cross.write_text(machine, encoding="utf-8", newline="\n")
        command = ["meson", "setup", str(build), str(root), "--cross-file", str(cross),
                   "--buildtype=release", "--default-library=static", "-Db_staticpic=true",
                   "--wrap-mode=nofallback"]
        if (build / "meson-private/coredata.dat").exists():
            # Dependency prefixes can be rebuilt with different platform features.
            command.extend(["--reconfigure", "--clearcache"])
        run(*command)
        receipt = prefix / "build-receipt.json"
        stamp = build / "torrview-dependencies.json"
        if receipt.exists() and (not stamp.exists() or stamp.read_bytes() != receipt.read_bytes()):
            # Header archives preserve old timestamps; Ninja cannot detect a version downgrade.
            run("meson", "compile", "-C", build, "--clean")
        run("meson", "compile", "-C", build, "torrview")
        if receipt.exists():
            stamp.write_bytes(receipt.read_bytes())
        library = build / "android/native/library/libtorrview.so"
        verify_elf(library, abi)
        dynamic = subprocess.check_output([str(binary / ("llvm-readelf" + suffix)), "-d", str(library)], text=True)
        for line in dynamic.splitlines():
            if "(NEEDED)" in line:
                name = line.split("[")[1].split("]")[0]
                if name not in SYSTEM_LIBS:
                    raise SystemExit(f"Unpackaged runtime dependency {name} in {library}")
        output = args.output / abi
        output.mkdir(parents=True, exist_ok=True)
        shutil.copy2(library, output / library.name)
        # Preserve symbols in Meson's output for native debugging, package the smaller copy.
        run(binary / ("llvm-strip" + suffix), "--strip-unneeded", output / library.name)
        verify_elf(output / library.name, abi)


if __name__ == "__main__":
    main()
