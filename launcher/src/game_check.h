// Is this the player's copy of the game the PC version is made from?
//
// The game's default.xex (XEX2, big-endian) names the game in its execution
// info (optional header 0x00040006): media id, version, base version, title
// id, platform, executable table, disc number and disc count. The recompiled
// code only fits the exact executables it was made from, so beyond the title
// and version, default.xex and the two DLC modules the game loads are checked
// by their SHA-256.

#pragma once

#include <cstdint>
#include <string>

namespace launcher {

class GameSource;

struct GameCheck {
  enum class Result {
    kFound,         // 2014 FIFA World Cup Brazil, the release this is made from
    kNoGame,        // no default.xex (or not an Xbox 360 executable)
    kOtherGame,     // an Xbox 360 game, but another one
    kOtherVersion,  // this game, another version (or disc)
    kChanged,       // the right version, but an executable differs from the disc
    kIncomplete,    // the right default.xex, but a DLC module is missing
  };
  Result result = Result::kNoGame;
  uint32_t title_id = 0;
  uint32_t media_id = 0;
  std::string version;  // "0.0.0.13"
  int disc = 0, discs = 0;
  std::string file;  // the changed or missing file, for kChanged / kIncomplete
};

// The title and version the PC version is made from.
constexpr uint32_t kGameTitleId = 0x454109D4;
inline constexpr const char* kGameVersion = "0.0.0.13";

GameCheck CheckGame(const GameSource& source);

}  // namespace launcher
