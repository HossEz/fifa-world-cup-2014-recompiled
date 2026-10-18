# Makes a release from the current builds: the release folder (launcher, game,
# runtime DLLs, notice; never any of the game's own files), a portable .zip of
# it, the installer (Setup.exe, if Inno Setup 6 is installed) and the debug
# symbols kept apart for reading crash reports.
#
#   powershell -File installer\package.ps1 [-GameBuild <dir>] [-LauncherBuild <dir>]
#
# Output in out\release\<version>\ (ignored by git). The version is in VERSION.

param(
  [string]$GameBuild = "pc\out\build\win-amd64-release",
  [string]$LauncherBuild = "launcher\out",
  [string]$Out = "out\release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$version = (Get-Content "VERSION" -Raw).Trim()
$name = "fifa-world-cup-2014-recompiled-$version"
$release = Join-Path $Out $version
# Without the version: it's the folder the portable zip unpacks to, and a
# portable copy updates itself in place, where a versioned name would go stale.
$folder = Join-Path $release "fifa-world-cup-2014-recompiled"
$symbols = Join-Path $release "$name-symbols"

function Need([string]$path) {
  if (-not (Test-Path $path)) { throw "Missing: $path (build it first)" }
  Get-Item $path
}

# What goes in: the launcher, the game and its recompiled DLC modules, the
# SDK's runtime and GPU plugin, and Microsoft's C++ runtime (app-local, so
# no separate Visual C++ redistributable install is needed).
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

Write-Host "Release $version from:"
foreach ($file in $files) { Write-Host ("  {0,-36} {1:yyyy-MM-dd HH:mm}" -f $file.Name, $file.LastWriteTime) }

if (Test-Path $release) { Remove-Item -Recurse -Force $release }
New-Item -ItemType Directory -Force $folder, $symbols | Out-Null
foreach ($file in $files) { Copy-Item $file.FullName $folder }
Copy-Item "installer\NOTICE.txt" $folder
Copy-Item "LICENSE" (Join-Path $folder "LICENSE.txt")
Copy-Item "licenses\THIRD-PARTY-NOTICES.txt" $folder

# The PDBs of what we built, for crash reports (not shipped in the release).
foreach ($file in $files) {
  $pdb = [IO.Path]::ChangeExtension($file.FullName, ".pdb")
  if (Test-Path $pdb) { Copy-Item $pdb $symbols }
}

# Never any of the game's own files, nor what's made from them on a player's
# PC (the icon, the font copy), nor a stray config or log.
$forbidden = "*.xex", "*.xexp", "*.xex.dll", "*.big", "*.bh", "*.iso", "*.vp6", "*.xur", "*.dds",
  "*.ico", "*.ttf", "*.toml", "*.log"
$found = Get-ChildItem $folder -Recurse -Include $forbidden
if ($found) { throw "Not for release: $($found.Name -join ', ')" }

# The portable zip has portable.txt, which keeps its settings next to it; the
# installer's copy doesn't (its settings go to Documents).
Copy-Item "installer\portable.txt" $folder
Compress-Archive -Path $folder -DestinationPath (Join-Path $release "$name-portable.zip")
Remove-Item (Join-Path $folder "portable.txt")
Compress-Archive -Path "$symbols\*" -DestinationPath (Join-Path $release "$name-symbols.zip")
Remove-Item -Recurse -Force $symbols

$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") |
  Where-Object { Test-Path $_ } | Select-Object -First 1
if ($iscc) {
  & $iscc /Q "/DAppVersion=$version" "/DSourceDir=$((Resolve-Path $folder).Path)" `
    "/DOutputDir=$((Resolve-Path $release).Path)" "installer\setup.iss"
  if ($LASTEXITCODE) { throw "Inno Setup failed ($LASTEXITCODE)" }
} else {
  Write-Warning "Inno Setup 6 not installed: no Setup.exe (winget install JRSoftware.InnoSetup)"
}

# Checksums of what players download, for the launcher's updater (it refuses a
# download that doesn't match). Published with the release.
$sums = Get-ChildItem $release -File | Where-Object { $_.Name -like "*-setup.exe" -or $_.Name -like "*-portable.zip" } |
  ForEach-Object { "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name }
[IO.File]::WriteAllText((Join-Path $release "SHA256SUMS.txt"), (($sums -join "`n") + "`n"))

Write-Host "`nIn $release :"
Get-ChildItem $release -File | ForEach-Object { Write-Host ("  {0,-36} {1,8:N1} MB" -f $_.Name, ($_.Length / 1MB)) }
