// The program's icon, made from the player's own copy of the game: none ships
// with the release (its files keep Windows' plain icon). Once the game is
// found the launcher writes icon.ico into its data folder from the game's cover tile
// (the EA Sports logo over the game's title), and from then on its window,
// the game's window (pc/src), the Start menu and desktop shortcuts and the
// Apps list (the installer points them at the file) use it.

#pragma once

#include <filesystem>

namespace launcher {

class GameFiles;

// The game picture the icon is made from (kept in launcher.toml, so an icon
// made from an earlier choice is made again).
const char* IconSource();

// icon.ico in the data folder (the settings' folder, see DataFolder in main.cpp).
std::filesystem::path IconPath(const std::filesystem::path& data_folder);

// Writes the icon (16 to 256 pixels) from the game's art. False if the art
// isn't there or the file can't be written.
bool WriteGameIcon(const GameFiles& files, const std::filesystem::path& ico);

// The icon on a window's title bar and taskbar button (if the file exists).
void SetWindowIcon(void* window, const std::filesystem::path& ico);

// Has Windows show the new icon on the shortcuts, which the installer points
// at icon.ico before it exists.
void RefreshShellIcons();

}  // namespace launcher
