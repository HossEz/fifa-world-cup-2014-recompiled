#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

namespace launcher {

namespace {
std::FILE* g_log = nullptr;
std::mutex g_log_mutex;

// Players post their logs in public: C:\Users\Name\... becomes C:\Users\<user>\...
void HideUserFolder(std::string& text) {
  for (size_t i = 0; i + 7 <= text.size(); ++i) {
    char separator = text[i];
    if ((separator != '\\' && separator != '/') || text[i + 6] != separator ||
        _strnicmp(text.c_str() + i + 1, "users", 5) != 0) {
      continue;
    }
    size_t end = text.find_first_of("\\/'\"", i + 7);
    if (end == std::string::npos) end = text.size();
    text.replace(i + 7, end - (i + 7), "<user>");
    i += 7 + 6 - 1;
  }
}
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
  va_list measure;
  va_copy(measure, args);
  int length = std::vsnprintf(nullptr, 0, format, measure);
  va_end(measure);
  std::string text(length > 0 ? size_t(length) : 0, '\0');
  if (length > 0) std::vsnprintf(text.data(), text.size() + 1, format, args);
  va_end(args);
  HideUserFolder(text);
  std::fputs(text.c_str(), g_log);
  std::fputc('\n', g_log);
  std::fflush(g_log);
}

}  // namespace launcher
