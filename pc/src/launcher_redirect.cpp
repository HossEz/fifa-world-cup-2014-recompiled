#include "launcher_redirect.h"

#include <rex/filesystem.h>
#include <rex/logging.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

void OpenLauncherIfNoGame(const std::filesystem::path& game_data_root) {
  std::error_code ec;
  if (!game_data_root.empty() && std::filesystem::exists(game_data_root, ec)) return;
#ifdef _WIN32
  std::filesystem::path launcher = rex::filesystem::GetExecutableFolder() / "Launcher.exe";
  if (!std::filesystem::exists(launcher, ec)) return;
  ShellExecuteW(nullptr, L"open", launcher.c_str(), nullptr, launcher.parent_path().c_str(),
                SW_SHOWNORMAL);
  ExitProcess(0);
#endif
}
