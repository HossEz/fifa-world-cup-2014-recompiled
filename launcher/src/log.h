// The launcher's log (launcher.log next to the game), started fresh each run:
// what was found and what failed, for reports and debugging.

#pragma once

#include <filesystem>

namespace launcher {

void OpenLog(const std::filesystem::path& path);
// printf-style; a line per call.
void Log(const char* format, ...);

}  // namespace launcher
