#include "data_folder.h"

#include <rex/filesystem.h>

std::filesystem::path UserFolder() {
  std::filesystem::path documents = rex::filesystem::GetUserFolder();
  std::filesystem::path folder = documents / "2014 FIFA World Cup Brazil";
  std::filesystem::path old = documents / "fifawc14";
  std::error_code ec;
  if (!std::filesystem::exists(folder, ec) && std::filesystem::is_directory(old, ec)) {
    std::filesystem::rename(old, folder, ec);
  }
  return folder;
}

std::filesystem::path SavesFolder() {
  std::filesystem::path exe_folder = rex::filesystem::GetExecutableFolder();
  std::error_code ec;
  if (std::filesystem::exists(exe_folder / "portable.txt", ec)) return exe_folder / "saves";
  return UserFolder();
}

std::filesystem::path DataFolder() {
  std::filesystem::path exe_folder = rex::filesystem::GetExecutableFolder();
  std::error_code ec;
  if (std::filesystem::exists(exe_folder / "portable.txt", ec) ||
      std::filesystem::exists(exe_folder / "fifawc14.toml", ec)) {
    return exe_folder;
  }
  std::filesystem::path folder = UserFolder();
  std::filesystem::create_directories(folder, ec);
  return folder;
}
