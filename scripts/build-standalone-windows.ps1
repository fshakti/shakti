# Build shakti_standalone on Windows using MSYS2 MinGW (UCRT64), not MSYS /usr GCC.
# Requires: CMake on PATH or under Program Files; MSYS2 with mingw-w64-ucrt-x86_64-gcc + ninja.
$ErrorActionPreference = "Stop"
# This file lives in shakti/scripts/
$shaktiRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $shaktiRoot "CMakeLists.txt"))) {
  Write-Error "Expected CMakeLists.txt at repo root (got $shaktiRoot)."
}

$cmake = $null
$pf86 = [Environment]::GetFolderPath("ProgramFilesX86")
foreach ($c in @(
    (Join-Path $env:ProgramFiles "CMake\bin\cmake.exe"),
    (Join-Path $pf86 "CMake\bin\cmake.exe")
  )) {
  if ($c -and (Test-Path $c)) { $cmake = $c; break }
}
if (-not $cmake) {
  $cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
}
if (-not $cmake) {
  Write-Error "cmake.exe not found. Install CMake or add to PATH."
}

foreach ($bin in @(
    "C:\msys64\ucrt64\bin",
    "C:\msys32\ucrt64\bin",
    "C:\msys64\mingw64\bin",
    "C:\msys32\mingw64\bin"
  )) {
  if (-not (Test-Path $bin)) { continue }
  $hasGcc = Test-Path (Join-Path $bin "gcc.exe")
  $hasNinja = Test-Path (Join-Path $bin "ninja.exe")
  $hasMingwMake = Test-Path (Join-Path $bin "mingw32-make.exe")
  if ($hasGcc -or $hasNinja -or $hasMingwMake) {
    $env:Path = "$bin;$env:Path"
  }
}

$buildDir = Join-Path $shaktiRoot "build\win-mingw-release"
$toolchain = Join-Path $shaktiRoot "cmake\toolchain-mingw64.cmake"

Push-Location $shaktiRoot
try {
  $haveNinja = [bool](Get-Command ninja -ErrorAction SilentlyContinue)
  $haveMingwMake = [bool](Get-Command mingw32-make -ErrorAction SilentlyContinue)
  if ($haveNinja) {
    & $cmake --preset windows-mingw-release
  } elseif ($haveMingwMake) {
    & $cmake -S . -B $buildDir -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_TOOLCHAIN_FILE="$toolchain"
  } else {
    Write-Error "Need Ninja or mingw32-make on PATH (MSYS2 UCRT64). Run: pacman -S mingw-w64-ucrt-x86_64-ninja  OR  mingw-w64-ucrt-x86_64-make"
  }
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  & $cmake --build $buildDir --target shakti_standalone
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  Write-Host "OK: shakti_standalone -> $buildDir/"
  Write-Host "Run from PowerShell:  `$env:Path = 'C:\msys64\mingw64\bin;' + `$env:Path   (or matching MinGW prefix) then .\shakti.exe ..."
} finally {
  Pop-Location
}
