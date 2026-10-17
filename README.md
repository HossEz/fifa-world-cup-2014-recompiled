# 2014 FIFA World Cup Brazil: Recompiled

An unofficial PC port of the Xbox 360 game *2014 FIFA World Cup Brazil*. The
game's code was translated ahead of time into C++ and compiled for Windows with
[ReXGlue](https://github.com/rexglue/rexglue-sdk): it runs natively, not in an
emulator.

**You need your own copy of the game.** No game files are included in this
repository or its releases.

## Playing

1. Download `fifa-world-cup-2014-recompiled-<version>-setup.exe` from the
   [Releases page](https://github.com/HossEz/fifa-world-cup-2014-recompiled/releases)
   and run it. It installs for your Windows user only (no admin rights needed)
   and opens the launcher. Prefer no install? Take the `-portable.zip`, unzip
   it anywhere and run `Launcher.exe`.

   The program isn't code-signed, so Windows may show "Windows protected your
   PC" the first time: choose **More info**, then **Run anyway**.
2. The launcher asks for your copy of the game: its folder (the one with
   `default.xex`) or its disc image (`.iso`). Choose it or drop it on the
   window; it checks that it's the right game and version.
3. Press **Play**.

The launcher's Settings set how the game runs (window, resolution, frame
rate, Show FPS, ...) and where it's read from. A controller is recommended
(Xbox, PlayStation and others); the keyboard works too, its keys are on the
Controls page.

### Requirements

| | |
|---|---|
| Windows | 10 or 11, 64-bit |
| Graphics | Direct3D 12, with up-to-date drivers |
| Processor | with AVX2 |
| The game | *2014 FIFA World Cup Brazil* for Xbox 360, as a folder or `.iso`: the release with media ID `5D31A745`, version 0.0.0.13 (checked with the European disc, En/De/It). The launcher says if yours is another version. |

### Where things are

| | |
|---|---|
| Saves, settings, shader cache | `Documents\2014 FIFA World Cup Brazil` (kept on uninstall) |
| The program, its logs | `%LOCALAPPDATA%\Programs\fifa-world-cup-2014-recompiled` |

Problems go in the [Issues](https://github.com/HossEz/fifa-world-cup-2014-recompiled/issues).
Include `fifawc14.log` from the program's folder (the launcher's Settings,
Data, Logs opens it) and the version from the launcher's About page.

### Known issues

- 1440p and 4K cost a lot of speed in cutscenes (down to ~20 FPS); 720p, the
  default, runs at a steady 60.
- Controller vibration isn't felt yet.

## Building from source

Building needs your own copy of the game too: the code generator reads its
`default.xex`.

The steps (SDK, game, launcher, a release) are in [`BUILDING.md`](BUILDING.md).

## Repository

| Folder | What it is |
|---|---|
| [`pc/`](pc/) | The game: the Windows app (`src/`) and the game fixes (`fixes/`: start-up races, GPU readback hooks, frame pacing). |
| [`launcher/`](launcher/) | The launcher: game check, menu, settings (native, Direct3D 11 + Dear ImGui). |
| [`installer/`](installer/) | Packaging: the release folder, the portable zip and the installer. |
| [`patches/rexglue-sdk/`](patches/rexglue-sdk/) | Changes to the ReXGlue SDK, applied on top of a fixed SDK commit. |
| [`tools/`](tools/) | Analysis helpers used while porting, and the licence collector. |
| [`licenses/`](licenses/) | The third-party components' licences (also in every release). |

The C++ translated from the game (`pc/generated/`) is produced locally from your
own copy of the game and is never committed.

## Licence

This project's own code (launcher, game fixes, SDK patches, tools) is under
the [BSD 3-Clause licence](LICENSE). It doesn't cover the game, its content, or
the code recompiled from it. The third-party components in the releases (the
ReXGlue SDK, which builds on Xenia, FFmpeg, SDL and others) keep their own
licences: see [`licenses/THIRD-PARTY-NOTICES.txt`](licenses/THIRD-PARTY-NOTICES.txt).

## Legal

Unofficial fan project. Not affiliated with, endorsed or sponsored by Electronic
Arts, FIFA or Microsoft. *2014 FIFA World Cup Brazil*, its content and trademarks
belong to their owners. If a rights holder raises a valid legal concern, it will be
addressed.
