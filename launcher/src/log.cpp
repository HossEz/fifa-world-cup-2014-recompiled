#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace launcher {

namespace {
std::FILE* g_log = nullptr;
std::mutex g_log_mutex;
}  // namespace

void OpenLog(const std::filesystem::path& path) {
  g_log = _wfopen(path.c_str(), L"w");
}

void Log(const char* format, ...) {
  std::lock_guard lock(g_log_mutex);
  if (!g_log) return;
  std::time_t now = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%H:%M:%S", std::localtime(&now));
  std::fprintf(g_log, "[%s] ", stamp);
  va_list args;
  va_start(args, format);
  std::vfprintf(g_log, format, args);
  va_end(args);
  std::fputc('\n', g_log);
  std::fflush(g_log);
}

}  // namespace launcher
