#pragma once

#include <filesystem>

// fifawc14.exe started without a game to play (no game_data_root, or it's gone):
// opens the launcher next to it instead, which finds the game, and ends this
// process. A player who double-clicks the game's exe gets the launcher rather
// than the SDK's "game_data_root was not provided" error. Returns if there's a
// game, or no launcher to open.
// Started by the launcher, it ends with kNoGameExitCode instead (launcher/src/main.cpp).
void OpenLauncherIfNoGame(const std::filesystem::path& game_data_root);

constexpr unsigned kNoGameExitCode = 0x0F140001;
