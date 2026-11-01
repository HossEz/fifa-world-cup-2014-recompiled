// The session report at the top of fifawc14.log: what a bug report needs to know
// without asking the player. Version, processor, memory, screen, game data type and
// the settings in effect.
//
// Players post their logs in public, so nothing here names the person or the PC:
// no paths (settings holding one are left out), no user or computer name.

#include "system_report.h"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace {

// The settings a report always needs, also at their default values.
constexpr const char* kKeySettings[] = {
    "resolution_scale",        "present_effect",        "fullscreen",
    "fullscreen_exclusive",    "video_mode_refresh_rate", "vsync_to_display",
    "frame_rate_floor",        "present_interval_cap",  "present_late_tolerance",
    "anisotropic_override",
};

std::string ProcessorName() {
  int regs[4] = {};
  __cpuid(regs, 0x80000000);
  if (uint32_t(regs[0]) < 0x80000004) {
    return "unknown";
  }
  char name[49] = {};
  for (int i = 0; i < 3; ++i) {
    __cpuid(regs, 0x80000002 + i);
    std::memcpy(name + i * 16, regs, 16);
  }
  std::string text(name);
  size_t first = text.find_first_not_of(' ');
  if (first == std::string::npos) {
    return "unknown";
  }
  return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

uint32_t PhysicalCores() {
  DWORD size = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size) {
    return 0;
  }
  std::vector<uint8_t> buffer(size);
  auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
  if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &size)) {
    return 0;
  }
  uint32_t cores = 0;
  for (DWORD offset = 0; offset < size;) {
    auto* entry =
        reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
    ++cores;
    offset += entry->Size;
  }
  return cores;
}

bool HoldsPath(std::string_view value) {
  return value.find('/') != std::string_view::npos || value.find('\\') != std::string_view::npos;
}

std::string Settings() {
  std::vector<std::string> names(std::begin(kKeySettings), std::end(kKeySettings));
  for (const std::string& name : rex::cvar::ListModifiedFlags()) {
    const rex::cvar::FlagEntry* flag = rex::cvar::GetFlagInfo(name);
    // The keyboard layout is long and says little.
    if (flag && flag->category.find("Input") == std::string::npos &&
        name.rfind("mnk_", 0) != 0 && name != "from_launcher" &&
        std::find(names.begin(), names.end(), name) == names.end()) {
      names.push_back(name);
    }
  }
  std::string text;
  for (const std::string& name : names) {
    if (!rex::cvar::GetFlagInfo(name)) {
      continue;
    }
    std::string value = rex::cvar::GetFlagByName(name);
    if (!HoldsPath(value)) {
      text += fmt::format(" {}={}", name, value);
    }
  }
  return text;
}

}  // namespace

void LogSystemReport(const std::filesystem::path& game_data_root) {
  REXLOG_INFO("2014 FIFA World Cup Brazil: Recompiled {}", GAME_VERSION);
  SYSTEM_INFO system = {};
  GetNativeSystemInfo(&system);
  REXLOG_INFO("processor: {}, {} cores, {} threads", ProcessorName(), PhysicalCores(),
              system.dwNumberOfProcessors);
  MEMORYSTATUSEX memory = {sizeof(memory)};
  if (GlobalMemoryStatusEx(&memory)) {
    REXLOG_INFO("memory: {} MB, {} MB free", memory.ullTotalPhys >> 20, memory.ullAvailPhys >> 20);
  }
  REXLOG_INFO("screen: {}x{}", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
  std::error_code ec;
  REXLOG_INFO("game data: {}",
              std::filesystem::is_directory(game_data_root, ec) ? "folder" : "disc image");
  REXLOG_INFO("settings:{}", Settings());
}
