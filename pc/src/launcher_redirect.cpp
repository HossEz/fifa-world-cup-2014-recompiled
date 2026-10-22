#include "launcher_redirect.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

// A command-line flag, not an environment variable: reading those is one of
// the traits antivirus models count against a program (Microsoft flagged the
// 0.1.7 setup with GetEnvironmentVariableW in the game).
REXCVAR_DEFINE_BOOL(from_launcher, false, "Game",
                    "Started by the launcher's Play (it passes --from_launcher)");

void OpenLauncherIfNoGame(const std::filesystem::path& game_data_root) {
  std::error_code ec;
  if (!game_data_root.empty() && std::filesystem::exists(game_data_root, ec)) return;
#ifdef _WIN32
  // Started by the launcher's Play: it already found the game and is waiting
  // for this process, so a second launcher only looked like Play opening the
  // menu again. The exit code tells it what happened.
  if (REXCVAR_GET(from_launcher)) {
    REXLOG_ERROR("No game at game_data_root '{}' (from the config, or it couldn't be read)",
                 rex::path_to_utf8(game_data_root));
    ExitProcess(kNoGameExitCode);
  }
  std::filesystem::path launcher = rex::filesystem::GetExecutableFolder() / "Launcher.exe";
  if (!std::filesystem::exists(launcher, ec)) return;
  ShellExecuteW(nullptr, L"open", launcher.c_str(), nullptr, launcher.parent_path().c_str(),
                SW_SHOWNORMAL);
  ExitProcess(0);
#endif
}
