# Windows Development Build

Use this profile for fast Windows development and testing. It uses dynamic MSYS2 system packages, disables static packaging, and refuses Meson subproject fallbacks so it does not spend time building bundled media dependencies.

From PowerShell:

```powershell
.\scripts\build-windows-dev.ps1
```

Or from an MSYS2 UCRT64 shell:

```sh
./scripts/build-windows-dev-msys2.sh
```

The default output is:

```text
build-windows-dev/src/torrview-dev.exe
```

Run `torrview-dev.exe` from an MSYS2 shell, or make sure the matching MSYS2 `bin` directory is on `PATH`, so its runtime DLLs can be found.

`torrview-dev.exe` is a Windows console application, so logs stay visible while developing and testing. The normal `torrview.exe` target remains the production GUI application and does not open a command window.
