  <div align="center">

  # 2014 FIFA World Cup Brazil: Recompiled

<p align="center">
  <a href="https://github.com/HossEz/fifa-world-cup-2014-recompiled/releases/latest">
    <img src="https://img.shields.io/badge/Download-latest%20release-2ea44f?style=for-the-badge&logo=github&logoColor=white" alt="Download latest release">
  </a>
</p>

<p align="center">
  <a href="https://github.com/HossEz/fifa-world-cup-2014-recompiled/releases"><img src="https://img.shields.io/github/downloads/HossEz/fifa-world-cup-2014-recompiled/total?style=flat-square&color=2ea44f&label=Downloads" alt="Total downloads"></a>
  <a href="https://github.com/HossEz/fifa-world-cup-2014-recompiled/stargazers"><img src="https://img.shields.io/github/stars/HossEz/fifa-world-cup-2014-recompiled?style=flat-square&color=f1c40f&label=Stars" alt="GitHub stars"></a>
  <a href="https://github.com/HossEz/fifa-world-cup-2014-recompiled/releases/latest"><img src="https://img.shields.io/github/v/release/HossEz/fifa-world-cup-2014-recompiled?style=flat-square&color=0078d4&label=Version" alt="Latest version"></a>
  <a href="#requirements"><img src="https://img.shields.io/badge/Platform-Windows%2010%2F11-0078d4?style=flat-square" alt="Platform: Windows 10/11"></a>
</p>

  An *unofficial* PC port of the Xbox 360 game **2014 FIFA World Cup Brazil**, by ***hosse***. The game's code was statically recompiled into C++ using [ReXGlue](https://github.com/rexglue/rexglue-sdk) and compiled for Windows, allowing it to run natively.

  The only official download is this page's [Releases](https://github.com/HossEz/fifa-world-cup-2014-recompiled/releases).

  **You need your own copy of the game.** No game files are included in this repository or its releases.

  </div>
  
  <img width="1100" height="700" alt="A screenshot of the launcher" src="https://github.com/user-attachments/assets/b4625ef4-2b4c-4930-9465-ddfba3f16aa7" />

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

  To quit the game, hold **Back + Start** (**Create + Options** on PlayStation) for 2 seconds, or press **Alt+F4**.

  The launcher's Settings set how the game runs (window, resolution, frame
  rate, Show FPS, ...) and where it's read from. A controller is recommended
  (Xbox, PlayStation and others). The keyboard works too, and its keys are on the
  Controls page.

  <img width="1920" height="1080" alt="An in-game screenshot" src="https://github.com/user-attachments/assets/416c4381-1563-47bc-a1b3-d5dd63544605" />

  <sub>In-game · 2014 FIFA World Cup Brazil: Recompiled</sub>

  ### Updates

  At the start the launcher checks this page for a newer release and offers it.
  **Update now** downloads it, checks it against the release's checksums and
  installs it (the installed version asks Windows for permission, as when it was
  installed, and the portable version replaces its own files). Settings and saves stay.
  Settings → Data → Check for updates turns this off.

  ### Requirements

  The game is heavy on the graphics card, more than on the processor. Integrated
  graphics will run it poorly, and 1440p or 4K needs a stronger card.

  | Requirement | Details |
  |---|---|
  | Windows | 10 or 11, 64-bit |
  | Graphics | Direct3D 12, with up-to-date drivers |
  | Processor | With SSE4.2 (Intel from 2008, AMD from 2011) |
  | The game | Not included: your own copy of *2014 FIFA World Cup Brazil* for Xbox 360, as a disc image (`.iso`) or its folder with `default.xex` (the launcher checks if your copy is playable) |

  ### Where things are (by default)

  | Item | Location |
  |---|---|
  | The program | `C:\Program Files\2014 FIFA World Cup Brazil Recompiled` |
  | Saves, settings, logs, shader cache | `Documents\2014 FIFA World Cup Brazil` (the portable version keeps it all in its own folder) |

  Problems go in the [Issues](https://github.com/HossEz/fifa-world-cup-2014-recompiled/issues).
  Include `fifawc14.log` (Settings → Data → Logs opens its folder) and the version from the launcher's About page.

  ### Known issues

  - Cutscenes, replays and celebrations can still drop below 60fps at high resolutions.
  - Intel UHD and Iris Xe graphics still run matches well below 60fps. The game doesn't draw correctly on these GPUs due to an issue on Intel's side, so they use a slower mode that draws correctly but costs significantly more performance.

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
