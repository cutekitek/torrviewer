param(
  [string]$BuildDir = "build-windows-static",
  [string]$Msys2Root = "C:\msys64",
  [string]$Msystem = "UCRT64"
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
$bash = Join-Path $Msys2Root "usr\bin\bash.exe"
if (-not (Test-Path $bash)) {
  throw "MSYS2 was not found at $Msys2Root. Install MSYS2 or pass -Msys2Root C:\path\to\msys64."
}

$env:MSYSTEM = $Msystem
$env:CHERE_INVOKING = "1"

& $bash -lc "cd '$($repo.Path -replace '\\','/' )' && ./scripts/build-windows-static-msys2.sh '$BuildDir'"
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
