# Making a release

What a player downloads: `fifa-world-cup-2014-recompiled-<version>-setup.exe` (the installer) or
`fifa-world-cup-2014-recompiled-<version>-portable.zip` (the same folder, unzip and run
`Launcher.exe`). Neither contains any of the game: the launcher asks for the
player's own copy (folder or `.iso`) at the first start.

## Steps

1. Build the game's Release config (in `pc/`: `cmake --preset win-amd64-release`,
   then `cmake --build out/build/win-amd64-release`) and the launcher, and test them.
2. Set the version in `VERSION` (repo root).
3. Install Inno Setup 6 once: `winget install JRSoftware.InnoSetup`.
4. From the repo root:

   ```
   powershell -ExecutionPolicy Bypass -File installer\package.ps1
   ```

   It lists the builds it takes (with their dates: check they're the ones you
   tested), refuses if any of the game's own files (or a config or log) would
   go in, and writes to `out\release\<version>\`:

   | File | |
   |---|---|
   | `fifa-world-cup-2014-recompiled-<version>-setup.exe` | the installer, for the GitHub release |
   | `fifa-world-cup-2014-recompiled-<version>-portable.zip` | the same, without installing |
   | `fifa-world-cup-2014-recompiled-<version>-symbols.zip` | PDBs for reading crash reports: keep, don't publish |
   | `fifa-world-cup-2014-recompiled-<version>/` | the release folder both are made from |

## What's in the release folder

| File | From |
|---|---|
| `Launcher.exe` | `launcher/out` |
| `fifawc14.exe`, `fifawc14_*_xex.dll` | the game build (`pc/out/build/...`) |
| `rexruntime*.dll`, `rexgpu-xenos*.dll`, `TracyClient*.dll` | the SDK, copied there by the game build |
| `msvcp140*.dll`, `vcruntime140*.dll` | Visual Studio's C++ runtime (redistributable, app-local) |
| `NOTICE.txt` | `installer/NOTICE.txt`: the unofficial / own-copy note |
| `LICENSE.txt` | `LICENSE`: this project's licence (BSD 3-Clause) |
| `THIRD-PARTY-NOTICES.txt` | `licenses/`: every third-party component's licence. After an SDK update, rerun `python tools/collect_licenses.py <sdk checkout>` |

## The installer

`setup.iss`: installs for the current user (no admin prompt) into
`%LOCALAPPDATA%\Programs\fifa-world-cup-2014-recompiled`, with a Start menu
entry, a desktop shortcut and an entry in Windows' Apps list. It has one page:
`NOTICE.txt` and an Install button; then the launcher opens by itself, at its
"find your game" screen. Uninstalling
removes the program and the settings and logs it wrote next to itself; the
player's saves in `Documents\2014 FIFA World Cup Brazil` stay.

The display name is "2014 FIFA World Cup Brazil: Recompiled"; folders and
files use `fifa-world-cup-2014-recompiled` (`AppName` and `AppSlug` in
`setup.iss`); the shortcuts are named "2014 FIFA World Cup Brazil -
Recompiled" (`ShortcutName`), since Windows doesn't allow a colon in file
names. Keep `[Setup] AppId` (the GUID) as it is: Windows uses it to
recognise upgrades.

No icon ships: it would be the game's art. The shortcuts and the Apps list
point at `icon.ico`, which the launcher makes from the player's own copy when
the game is found (`launcher/src/icon.h`); until then Windows shows its plain
icon.
