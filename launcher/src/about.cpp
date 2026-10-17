#include "about.h"

#ifndef LAUNCHER_COMMIT
#define LAUNCHER_COMMIT "unknown"
#endif
#ifndef LAUNCHER_DATE
#define LAUNCHER_DATE ""
#endif

namespace launcher {

const std::vector<Topic>& AboutTopics() {
  static const std::vector<Topic> topics = {
      {"This project",
       {"2014 FIFA World Cup Brazil, the Xbox 360 game, running natively on Windows. An "
        "unofficial fan project.",
        "It is a static recompilation, not an emulator: the game's PowerPC code was translated "
        "ahead of time into C++ and compiled for your PC, so it runs the game's own logic "
        "directly instead of interpreting Xbox instructions."}},
      {"How it works",
       {"The recompiled code runs on a runtime that stands in for the console: the Xbox "
        "kernel's functions, its file system and its threads.",
        "Every frame, the Xenos GPU's commands are translated to Direct3D 12, its EDRAM render "
        "targets included. The game's XMA audio is decoded with FFmpeg. A few fixes on top let "
        "menus and cutscenes run at 60 FPS and keep the stadium lighting right without slowing "
        "the GPU down."}},
      {"What it needs",
       {"Windows 10 or 11, 64-bit, a graphics card with Direct3D 12 and up-to-date drivers, "
        "and a processor with AVX2.",
        "A controller is recommended; the keyboard works too (its keys are on the Controls "
        "page).",
        "Your own copy of the game: everything of the game's, including the art and music in "
        "this menu, is read from it."}},
      {"Credits",
       {"ReXGlue: the recompiler and runtime that turn the Xbox 360 code into C++ and run it.",
        "Xenia: the Xbox 360 emulator whose research and GPU and kernel code ReXGlue builds on.",
        "FFmpeg: decodes the game's XMA audio. Dear ImGui: draws this menu. libmspack and stb: "
        "read the game's compressed files. SDL: controllers."}},
      {"Legal",
       {"Not affiliated with or endorsed by Electronic Arts, EA Sports, FIFA or Microsoft. 2014 "
        "FIFA World Cup Brazil and its art, music and names belong to their owners.",
        "None of the game's files are included: its art, sound and data come from your own "
        "copy; its code is included only in recompiled form. If a rights holder raises a valid "
        "legal concern, it will be addressed.",
        "This project's code is under the BSD 3-Clause licence (LICENSE.txt); the components "
        "it uses keep theirs (THIRD-PARTY-NOTICES.txt, both in the program's folder)."}},
      {"Build",
       {"Mention this when reporting a problem: it says which version you're running."},
       true},
  };
  return topics;
}

std::string BuildLine() {
  std::string line = std::string("Build ") + LAUNCHER_COMMIT;
  if (*LAUNCHER_DATE) line += std::string(", ") + LAUNCHER_DATE;
  return line;
}

}  // namespace launcher
