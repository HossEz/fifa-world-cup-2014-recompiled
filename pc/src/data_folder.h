#pragma once

#include <filesystem>

// The player's folder: saves, the shader cache, and normally the settings and
// logs too. Documents\2014 FIFA World Cup Brazil; earlier builds used
// Documents\fifawc14, which is moved over once (the launcher does the same).
std::filesystem::path UserFolder();

// Where the settings (fifawc14.toml), logs, the icon and the font copy are:
// next to the exe in the portable version (portable.txt is there) or a
// developer build (a fifawc14.toml is there), otherwise UserFolder(). The same
// rule as the launcher's (launcher/src/main.cpp, DataFolder), so an installed
// game can sit in Program Files. Saves are always in UserFolder().
std::filesystem::path DataFolder();
