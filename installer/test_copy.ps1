# Makes a fresh portable copy from the current builds, to test a change before
# packaging a release: the same files as the release folder (package.ps1), plus
# portable.txt, in out\test\ (ignored by git). Everything from the previous test
# copy is removed (settings, saves, shader cache), so it starts like a new player's
# first run: choose the game, then Play.
#
#   powershell -ExecutionPolicy Bypass -File installer\test_copy.ps1 [-Run]
#
# -Run opens its Launcher.exe when it's made.

param(
  [string]$GameBuild = "pc\out\build\win-amd64-release",
  [string]$LauncherBuild = "launcher\out",
  [string]$Out = "out\test",
  [switch]$Run
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Need([string]$path) {
  if (-not (Test-Path $path)) { throw "Missing: $path (build it first)" }
  Get-Item $path
}

$files = @(
  Need "$LauncherBuild\Launcher.exe"
  Need "$GameBuild\fifawc14.exe"
)
$files += Get-ChildItem "$GameBuild\fifawc14_*_xex.dll"
$files += Get-ChildItem "$GameBuild\rex*.dll"
$files += Get-ChildItem "$GameBuild\TracyClient*.dll"
$redist = Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" |
  Sort-Object FullName | Select-Object -Last 1
if (-not $redist) { throw "Visual Studio 2022's C++ redistributable files not found" }
foreach ($dll in "msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll") {
  $files += Need (Join-Path $redist.FullName $dll)
}

Write-Host "Test copy from:"
foreach ($file in $files) { Write-Host ("  {0,-36} {1:yyyy-MM-dd HH:mm}" -f $file.Name, $file.LastWriteTime) }

if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force $Out | Out-Null
foreach ($file in $files) { Copy-Item $file.FullName $Out }
Copy-Item "installer\portable.txt" $Out

$launcher = Resolve-Path (Join-Path $Out "Launcher.exe")
Write-Host "Made $(Resolve-Path $Out)"
if ($Run) { Start-Process $launcher -WorkingDirectory (Split-Path $launcher) }
