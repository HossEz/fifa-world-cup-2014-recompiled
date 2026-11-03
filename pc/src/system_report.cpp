// The session report at the top of fifawc14.log: what a bug report needs to know
// without asking the player. Version, processor, memory, screen, power source and
// mode, game data type and the settings in effect.
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

// Laptops hold the GPU and CPU back on battery or in a power-saving mode, which looks
// like the game being slow. The power plan is the classic one (Balanced, High
// performance...); the power mode is the slider in Windows' power settings, which only
// applies to Balanced (PowerGetEffectiveOverlayScheme, Windows 10 1809 and later).
// powrprof.dll is looked up at run time.
std::string PowerState() {
  std::string text;
  SYSTEM_POWER_STATUS status = {};
  if (GetSystemPowerStatus(&status)) {
    bool has_battery = status.BatteryFlag != 128 && status.BatteryFlag != 255;
    if (!has_battery) {
      text = "no battery";
    } else {
      text = status.ACLineStatus == 1 ? "plugged in" : "on battery";
      if (status.BatteryLifePercent <= 100) {
        text += fmt::format(" {}%", status.BatteryLifePercent);
      }
      if (status.SystemStatusFlag & 1) {
        text += ", battery saver on";
      }
    }
  }
  using ActiveSchemeFn = DWORD(WINAPI*)(HKEY, GUID**);
  using OverlaySchemeFn = DWORD(WINAPI*)(GUID*);
  HMODULE powrprof = LoadLibraryExW(L"powrprof.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (powrprof) {
    // The power plan. The power mode slider only applies to the Balanced plan.
    static const GUID kPlanBalanced = {
        0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
    static const GUID kPlanHighPerformance = {
        0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
    static const GUID kPlanPowerSaver = {
        0xa1841308, 0x3541, 0x4fab, {0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a}};
    static const GUID kPlanUltimate = {
        0xe9a42b02, 0xd5df, 0x448d, {0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61}};
    bool balanced = true;
    auto get_active =
        reinterpret_cast<ActiveSchemeFn>(GetProcAddress(powrprof, "PowerGetActiveScheme"));
    GUID* plan = nullptr;
    if (get_active && get_active(nullptr, &plan) == ERROR_SUCCESS && plan) {
      const char* name = "custom";
      if (IsEqualGUID(*plan, kPlanBalanced)) {
        name = "balanced";
      } else if (IsEqualGUID(*plan, kPlanHighPerformance)) {
        name = "high performance";
      } else if (IsEqualGUID(*plan, kPlanPowerSaver)) {
        name = "power saver";
      } else if (IsEqualGUID(*plan, kPlanUltimate)) {
        name = "ultimate performance";
      }
      balanced = IsEqualGUID(*plan, kPlanBalanced);
      text += fmt::format("{}power plan {}", text.empty() ? "" : ", ", name);
      LocalFree(plan);
    }
    auto get_overlay = reinterpret_cast<OverlaySchemeFn>(
        GetProcAddress(powrprof, "PowerGetEffectiveOverlayScheme"));
    GUID overlay = {};
    if (balanced && get_overlay && get_overlay(&overlay) == ERROR_SUCCESS) {
      static const GUID kBestPerformance = {
          0xded574b5, 0x45a0, 0x4f42, {0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38}};
      static const GUID kBestEfficiency = {
          0x961cc777, 0x2547, 0x4f9d, {0x81, 0x74, 0x7d, 0x86, 0x18, 0x1b, 0x8a, 0x7a}};
      static const GUID kBetterBattery = {
          0x3af9b8d9, 0x7c97, 0x431d, {0xad, 0x78, 0x34, 0xa8, 0xbf, 0xea, 0x43, 0x9f}};
      const char* mode = "balanced";
      if (IsEqualGUID(overlay, kBestPerformance)) {
        mode = "best performance";
      } else if (IsEqualGUID(overlay, kBestEfficiency)) {
        mode = "best power efficiency";
      } else if (IsEqualGUID(overlay, kBetterBattery)) {
        mode = "better battery";
      }
      text += fmt::format("{}power mode {}", text.empty() ? "" : ", ", mode);
    }
    FreeLibrary(powrprof);
  }
  return text.empty() ? "unknown" : text;
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
  REXLOG_INFO("power: {}", PowerState());
  std::error_code ec;
  REXLOG_INFO("game data: {}",
              std::filesystem::is_directory(game_data_root, ec) ? "folder" : "disc image");
  REXLOG_INFO("settings:{}", Settings());
}
