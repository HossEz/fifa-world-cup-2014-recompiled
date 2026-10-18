  <div align="center">

  # 2014 FIFA World Cup Brazil: Recompiled

  An *unofficial* PC port of the Xbox 360 game ***2014 FIFA World Cup Brazil***, by **hosse**. The game's code was translated into C++ with
  [ReXGlue](https://github.com/rexglue/rexglue-sdk) and compiled for Windows, so it runs natively.

  The only official download is this page's [Releases](https://github.com/HossEz/fifa-world-cup-2014-recompiled/releases).

  **You need your own copy of the game.** No game files are included in this repository or its releases.

  </div>

  <img width="1100" height="700" alt="Launcher_MKPVs8uOWV" src="https://github.com/user-attachments/assets/3af53c69-0882-41d6-8bff-ccf03006d638" />
  <sub>Launcher preview · 2014 FIFA World Cup Brazil: Recompiled</sub>

  ## Playing

  1. Download `fifa-world-cup-2014-recompiled-<version>-setup.exe` from the
     [Releases page](https://github.com/HossEz/fifa-world-cup-2014-recompiled/releases)
     and run it. It installs into Program Files (Windows asks for permission
     once) and opens the launcher. Prefer no install? Take the
     `-portable.zip`, unzip it anywhere and run `Launcher.exe`. It keeps everything, saves included, in its own folder.
     Settings → Data → Desktop shortcut adds a desktop shortcut with the game's icon.

     > The program isn't code-signed, so Windows may show "Windows protected your PC" the first time. Choose **More info**, then **Run anyway**.
  2. The launcher asks for your copy of the game, either its folder (the one with
     `default.xex`) or its disc image (`.iso`). Choose it or drop it on the
     window, and it checks that it's the right game.
  3. Press **Play**.

  The launcher's Settings set how the game runs (window, resolution, frame
  rate, Show FPS, ...) and where it's read from. A controller is recommended
  (Xbox, PlayStation and others). The keyboard works too, and its keys are on the
  Controls page.

  <img width="1920" height="1080" alt="fifawc14_GtHsVXXJIM" src="https://github.com/user-attachments/assets/416c4381-1563-47bc-a1b3-d5dd63544605" />
  <sub>In-game · 2014 FIFA World Cup Brazil: Recompiled</sub>

  ### Updates

  At the start the launcher checks this page for a newer release and offers it.
  **Update now** downloads it, checks it against the release's checksums and
  installs it (the installed version asks Windows for permission, as when it was
  installed, and the portable version replaces its own files). Settings and saves stay.
  Settings → Data → Check for updates turns this off.

  ### Requirements

  | Requirement | Details |
  |---|---|
  | Windows | 10 or 11, 64-bit |
  | Graphics | Direct3D 12, with up-to-date drivers |
  | Processor | with AVX2 |
  | The game | *2014 FIFA World Cup Brazil* for Xbox 360, as a folder or `.iso` (the launcher checks if your copy is playable) |

  ### Where things are (by default)

  | Item | Location |
  |---|---|
  | The program | `C:\Program Files\2014 FIFA World Cup Brazil Recompiled` |
  | Saves, settings, logs, shader cache | `Documents\2014 FIFA World Cup Brazil` (the portable version keeps it all in its own folder) |

  Problems go in the [Issues](https://github.com/HossEz/fifa-world-cup-2014-recompiled/issues).
  Include `fifawc14.log` (Settings → Data → Logs opens its folder) and the version from the launcher's About page.

  ### Known issues

  - 1440p and 4K have performance issues, especially 4K. 720p, the default, runs smoothly.

  ## Building from source

  Building needs your own copy of the game too, because the code generator reads its
  `default.xex`.

  The steps (SDK, game, launcher, a release) are in [`BUILDING.md`](BUILDING.md).

   ## Repository
   
   | Folder | What it is |
   |---|---|
   | [`pc/`](pc/) | The game's Windows app (`src/`) and the game fixes (`fixes/`, for start-up races, GPU readback hooks and frame pacing). |
   | [`launcher/`](launcher/) | The launcher, which finds and checks the game and has the menu, settings and updates (native, Direct3D 11 + Dear ImGui). |
   | [`installer/`](installer/) | Packaging of the release folder, the portable zip, the installer and the checksums ([how a release is made](installer/README.md)). |
   | [`patches/rexglue-sdk/`](patches/rexglue-sdk/) | Changes to the ReXGlue SDK, applied on top of a fixed SDK commit. |
   | [`tools/`](tools/) | Analysis helpers used while porting, and the licence collector. |
   | [`licenses/`](licenses/) | The third-party components' licences (also in every release). |
   
   The C++ translated from the game (pc/generated/) isn't part of this repository. It's generated on your own PC from your own copy of the game when you build.

  ## Credits

  - The port by **hosse** *(me)*, developed with assistance from [Claude Code](https://claude.com/claude-code).
  - Built on [ReXGlue](https://github.com/rexglue/rexglue-sdk), the recompiler and runtime.
  - [Xenia](https://xenia.jp), whose Xbox 360 research, GPU and kernel code ReXGlue builds on.
  - The other components it uses are listed with their licences in
    [`licenses/THIRD-PARTY-NOTICES.txt`](licenses/THIRD-PARTY-NOTICES.txt).

  ## Licence

  This project's own code (launcher, game fixes, SDK patches, tools) is under
  the [BSD 3-Clause licence](LICENSE). It doesn't cover the game, its content, or
  the code recompiled from it. The third-party components in the releases (the
  ReXGlue SDK, which builds on Xenia, FFmpeg, SDL and others) keep their own
  licences, listed in [`licenses/THIRD-PARTY-NOTICES.txt`](licenses/THIRD-PARTY-NOTICES.txt).

  ## Legal

  Unofficial fan project. Not affiliated with, endorsed or sponsored by Electronic
  Arts, FIFA or Microsoft. *2014 FIFA World Cup Brazil*, its content and trademarks
  belong to their owners. If a rights holder raises a valid legal concern, it will be
  addressed.
