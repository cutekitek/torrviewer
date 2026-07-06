# Windows Static Build

This project has a Windows-only static profile that builds a custom size-optimized FFmpeg first, then builds bundled static `libmpv`, `libtorrent-rasterbar`, and ThorVG through Meson subprojects. The final executable is stripped and compressed with UPX by default.

The intended toolchain is MSYS2 MinGW UCRT64. From PowerShell:

```powershell
.\scripts\build-windows-static.ps1
```

Or from an MSYS2 UCRT64 shell:

```sh
./scripts/build-windows-static-msys2.sh
```

The FFmpeg build is intentionally decode-only and uses FFmpeg's `--enable-small` option by default. It keeps the libraries mpv requires (`avcodec`, `avformat`, `avfilter`, `avutil`, `swresample`, `swscale`) and enables common media demuxers/decoders for MKV, MP4/MOV, WebM, AVI, MPEG-TS/PS, FLV, Ogg, MP3, AAC, FLAC, H.264, HEVC, MPEG video, VP8/VP9, AV1, common Windows Media codecs, and common audio codecs. It also enables Windows D3D11VA/DXVA2 hardware decode paths for common GPU-decodable video formats. It disables FFmpeg programs, devices, network protocols, encoders, muxers, and documentation.

The bundled libplacebo build enables its OpenGL backend for GPU rendering support while keeping Vulkan and D3D11 disabled to avoid extra shader compiler and loader dependencies in the static package. The embedded libmpv build enables Windows D3D hardware decoding, and the app requests mpv's `hwdec=auto-safe` mode at runtime on Windows.

Override the FFmpeg revision with:

```powershell
$env:FFMPEG_REF = "n7.1.1"
.\scripts\build-windows-static.ps1
```

The size optimizations can be disabled for troubleshooting:

```powershell
$env:FFMPEG_ENABLE_SMALL = "0"
$env:ENABLE_UPX = "0"
.\scripts\build-windows-static.ps1
```

The script prints imported DLLs for `torrview.exe` after the build. A fully static MinGW build should only list Windows system DLLs such as `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `OPENGL32.dll`, and similar OS libraries.

`torrview.exe` is built as a Windows GUI application, so launching the production executable does not open a separate command window. Use the separate Windows development build for a fast console target.
