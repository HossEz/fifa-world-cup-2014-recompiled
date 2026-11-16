// The game's text as the PC build words it, in its language files (EA T3DB
// databases, data\loc\<language>.db in data\loc\locale.big).
//
// Always: the console is a PC ("Please do not turn off your PC"), and the hard
// drive, memory unit and headset lose "Xbox 360". With PlayStation buttons the
// controller is the Wireless Controller ("Wireless Controller 1"), as the PS3
// version calls it. Each line keeps its own capitals, and articles follow the
// new word's gender (German "deinen PC", French "le PC"). Kinect, achievements,
// Xbox Live and the Guide stay as they are.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// The language file `db` for `language` (its file name without .db, e.g.
// "ger_de") reworded, or nothing if no line changes (or the file isn't one).
// `changed`: the number of lines reworded.
std::optional<std::vector<uint8_t>> RewordLanguageFile(std::string_view language,
                                                       std::span<const uint8_t> db,
                                                       bool playstation, int* changed);
