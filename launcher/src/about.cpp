#include "about.h"

#include <map>

#include "i18n.h"

#ifndef LAUNCHER_VERSION
#define LAUNCHER_VERSION ""
#endif
#ifndef LAUNCHER_COMMIT
#define LAUNCHER_COMMIT ""
#endif
#ifndef LAUNCHER_DATE
#define LAUNCHER_DATE ""
#endif

namespace launcher {

const std::vector<Topic>& AboutTopics() {
  // Made once per language.
  static std::map<Language, std::vector<Topic>> made;
  std::vector<Topic>& topics = made[CurrentLanguage()];
  if (!topics.empty()) return topics;
  topics = {
      {T("This project"),
       {T("2014 FIFA World Cup Brazil, the Xbox 360 game, running natively on Windows. An "
          "unofficial fan project, ported by hosse."),
        T("The original, with its source and downloads, is on GitHub:"),
        "github.com/HossEz/fifa-world-cup-2014-recompiled",
        T("It is a static recompilation, not an emulator. The game's PowerPC code was translated "
          "into C++ and compiled for your PC, so it runs the game's own logic "
          "directly instead of interpreting Xbox instructions.")}},
      {T("How it works"),
       {T("The recompiled code runs on a runtime that stands in for the console, with the Xbox "
          "kernel's functions, its file system and its threads."),
        T("Every frame, the Xenos GPU's commands are translated to Direct3D 12, its EDRAM render "
          "targets included. The game's XMA audio is decoded with FFmpeg. A few fixes on top let "
          "menus and cutscenes run at 60 FPS and keep the stadium lighting right without slowing "
          "the GPU down.")}},
      {T("What it needs"),
       {T("Windows 10 or 11, 64-bit, a graphics card with Direct3D 12 and up-to-date drivers, "
          "and a processor with AVX2."),
        T("A controller is recommended; the keyboard works too (its keys are on the Controls "
          "page)."),
        T("Your own copy of the game. Everything of the game's, including the art and music in "
          "this menu, is read from it.")}},
      {T("Credits"),
       {T("Built on ReXGlue, the recompiler and runtime that turn the Xbox 360 code into C++ and "
          "run it."),
        T("Xenia, the Xbox 360 emulator whose research, GPU and kernel code ReXGlue builds on."),
        T("FFmpeg decodes the game's XMA audio, Dear ImGui draws this menu, libmspack and stb read "
          "the game's compressed files, and SDL handles controllers.")}},
      {T("Legal"),
       {T("Not affiliated with or endorsed by Electronic Arts, EA Sports, FIFA or Microsoft. 2014 "
          "FIFA World Cup Brazil and its art, music and names belong to their owners."),
        T("None of the game's files are included. Its art, sound and data come from your own "
          "copy, and its code is included only in recompiled form. If a rights holder raises a valid "
          "legal concern, it will be addressed."),
        T("This project's code is under the BSD 3-Clause licence (LICENSE.txt); the components "
          "it uses keep theirs (THIRD-PARTY-NOTICES.txt, both in the program's folder).")}},
      {T("Build"),
       {T("Mention this when reporting a problem, so it's clear which version you're running.")},
       true},
  };
  return topics;
}

const char* LauncherVersion() { return LAUNCHER_VERSION; }

std::string BuildLine() {
  std::string line = Tf("Version {0}", {*LAUNCHER_VERSION ? LAUNCHER_VERSION : "?"});
  std::string build = LAUNCHER_COMMIT;
  if (*LAUNCHER_DATE) build += std::string(build.empty() ? "" : ", ") + LAUNCHER_DATE;
  if (!build.empty()) line += "  \xC2\xB7  build " + build;
  return line;
}

}  // namespace launcher
