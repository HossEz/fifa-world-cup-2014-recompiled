// Is this the player's copy of the game the PC version is made from?
//
// The game's default.xex (XEX2, big-endian) names the game in its execution
// info (optional header 0x00040006): media id, version, base version, title
// id, platform, executable table, disc number and disc count. The recompiled
// code only fits the exact code it was made from, so default.xex and the two
// DLC modules the game loads are unpacked (decrypted and decompressed, as the
// console loads them) and their code compared by SHA-256. Releases whose
// executables differ only in their headers (region, media ID, version
// number), such as the European and the US disc, have the same code and are
// all accepted.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace launcher {

class GameSource;

struct GameCheck {
  enum class Result {
    kFound,       // 2014 FIFA World Cup Brazil with the code the PC version is made from
    kNoGame,      // no default.xex (or not an Xbox 360 executable)
    kOtherGame,   // an Xbox 360 game, but another one
    kOtherCode,   // this game, but an executable's code differs (another build, patched)
    kIncomplete,  // default.xex is there, but a DLC module is missing
  };
  Result result = Result::kNoGame;
  uint32_t title_id = 0;
  uint32_t media_id = 0;
  std::string version;  // "0.0.0.13"
  std::string region;   // "Europe", "USA" for the releases known to work; else empty
  std::vector<uint32_t> languages;  // the disc's (console language IDs), for those releases
  int disc = 0, discs = 0;
  std::string file;  // the differing or missing file, for kOtherCode / kIncomplete
};

// The title the PC version is made from.
constexpr uint32_t kGameTitleId = 0x454109D4;

GameCheck CheckGame(const GameSource& source);

}  // namespace launcher
