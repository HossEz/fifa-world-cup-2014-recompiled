// Per-thread CPU use, for finding which thread limits the frame rate.
//
// With log_thread_cpu = N (seconds), a background thread logs every N seconds
// the process threads that used at least 10% of a core in that window, busiest
// first, by name ("thread cpu:"). The game's own threads and the runtime's (GPU
// command processor, audio, vblank) are named, so a thread pinned near 100% in
// slow scenes is the one to optimise.

#include "thread_stats.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>

// On by default (every 30 s): players send logs, and a thread pinned near 100%
// (heat, low frame rates) is then in them.
REXCVAR_DEFINE_INT32(log_thread_cpu, 30, "Debug",
                     "Every N seconds log the threads that used at least 10% of a core, by "
                     "name. 0 = off.")
    .range(0, 3600);

namespace {

uint64_t FileTimeTo100ns(const FILETIME& time) {
  return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

std::string ThreadName(HANDLE thread) {
  PWSTR description = nullptr;
  std::string name;
  if (SUCCEEDED(GetThreadDescription(thread, &description)) && description) {
    int length = WideCharToMultiByte(CP_UTF8, 0, description, -1, nullptr, 0, nullptr, nullptr);
    if (length > 1) {
      name.resize(length - 1);
      WideCharToMultiByte(CP_UTF8, 0, description, -1, name.data(), length, nullptr, nullptr);
    }
    LocalFree(description);
  }
  return name.empty() ? "(unnamed)" : name;
}

void Run(int interval_seconds) {
  DWORD process_id = GetCurrentProcessId();
  std::unordered_map<DWORD, uint64_t> previous_cpu;
  auto previous_wall = std::chrono::steady_clock::now();
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
    auto now = std::chrono::steady_clock::now();
    double wall_100ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - previous_wall).count() / 100.0;
    previous_wall = now;

    struct Entry {
      double percent;
      DWORD id;
      std::string name;
    };
    std::vector<Entry> busy;
    std::unordered_map<DWORD, uint64_t> current_cpu;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
      continue;
    }
    THREADENTRY32 entry = {sizeof(entry)};
    for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
      if (entry.th32OwnerProcessID != process_id) {
        continue;
      }
      HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
      if (!thread) {
        continue;
      }
      FILETIME creation, exit, kernel, user;
      if (GetThreadTimes(thread, &creation, &exit, &kernel, &user)) {
        uint64_t cpu = FileTimeTo100ns(kernel) + FileTimeTo100ns(user);
        current_cpu[entry.th32ThreadID] = cpu;
        auto previous = previous_cpu.find(entry.th32ThreadID);
        if (previous != previous_cpu.end() && wall_100ns > 0) {
          double percent = (cpu - previous->second) * 100.0 / wall_100ns;
          if (percent >= 10.0) {
            busy.push_back({percent, entry.th32ThreadID, ThreadName(thread)});
          }
        }
      }
      CloseHandle(thread);
    }
    CloseHandle(snapshot);
    previous_cpu = std::move(current_cpu);

    std::sort(busy.begin(), busy.end(),
              [](const Entry& a, const Entry& b) { return a.percent > b.percent; });
    std::string text;
    for (const Entry& thread : busy) {
      text += fmt::format("  {} [t{}] {:.0f}%", thread.name, thread.id, thread.percent);
    }
    REXLOG_INFO("thread cpu ({} s):{}", interval_seconds, text.empty() ? " all under 10%" : text);
  }
}

}  // namespace

void StartThreadCpuLog() {
  int interval_seconds = REXCVAR_GET(log_thread_cpu);
  if (interval_seconds > 0) {
    std::thread(Run, interval_seconds).detach();
  }
}
