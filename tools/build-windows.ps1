<#
Build (and optionally package) F-Zero Forever for Windows x64 with MSYS2 MinGW.

  powershell -File tools\build-windows.ps1                     # development build + tests
  powershell -File tools\build-windows.ps1 --packs DIR ...     # + release .zip per edition

All arguments are passed to tools/build_release.py (run with --help for them).

Requirements: MSYS2 (https://www.msys2.org) with, in a MINGW64 shell:
  pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,libxml2,libpng,giflib,rapidjson,libarchive,SDL3}
and Python 3.12 for Windows (py launcher). Set MINGW_PREFIX if MSYS2 is not in
C:\msys64. The importer helper is frozen with PyInstaller from a private venv.
#>
$ErrorActionPreference = 'Stop'
$Repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Mingw = if ($env:MINGW_PREFIX -and (Test-Path -LiteralPath $env:MINGW_PREFIX)) { $env:MINGW_PREFIX } else { 'C:\msys64\mingw64' }
foreach ($tool in 'gcc.exe', 'g++.exe', 'cmake.exe', 'objdump.exe') {
    if (-not (Test-Path -LiteralPath (Join-Path $Mingw "bin\$tool"))) {
        throw "Missing $tool in $Mingw\bin; install the MSYS2 packages listed in tools\build-windows.ps1"
    }
}
# MinGW first so CMake, the compilers and runtime DLLs agree.
$env:PATH = (Join-Path $Mingw 'bin') + ';' + $env:PATH
$env:MINGW_PREFIX = $Mingw

$Venv = Join-Path $Repo 'build-tools-venv-windows'
$Python = Join-Path $Venv 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $Python)) {
    # Use the Windows Python, never MSYS2's: PyInstaller must freeze a native helper.
    $launcher = Get-Command py.exe -CommandType Application -ErrorAction SilentlyContinue
    if ($launcher) { & $launcher.Source -3.12 -m venv $Venv } else { & python.exe -m venv $Venv }
    if ($LASTEXITCODE -ne 0) { throw "Could not create $Venv" }
}
& $Python -m pip install --quiet --disable-pip-version-check -r (Join-Path $Repo 'tools\converter-requirements.txt')
if ($LASTEXITCODE -ne 0) { throw 'Installing converter requirements failed' }

& $Python -X utf8 (Join-Path $Repo 'tools\build_release.py') @args
exit $LASTEXITCODE
