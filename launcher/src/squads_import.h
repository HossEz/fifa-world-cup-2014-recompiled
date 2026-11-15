// Imports a squads file (such as EA's last squad update, July 2014) as one of
// the game's own squads saves. The game loads squads by itself at start, so
// the player doesn't save one first, rename files or load anything.
//
// Takes the Xbox 360 save package (CON) or the bare save inside one, as the
// game writes it in this port's saves folder.

#pragma once

#include <filesystem>

namespace launcher {

enum class SquadsImport { kOk, kCancelled, kNotSquads, kOtherGame, kWriteFailed };

// Asks for the file (over the launcher window) and imports it into
// `user_folder` (the saves folder, see SavesFolder in main.cpp).
SquadsImport PickAndImportSquads(const std::filesystem::path& user_folder);

}  // namespace launcher
