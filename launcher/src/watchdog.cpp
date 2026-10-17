#include "watchdog.h"

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "log.h"

namespace launcher {

namespace {

std::atomic<int64_t> g_last_tick_ms{0};
HWND g_window = nullptr;
HANDLE g_main_thread = nullptr;

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void LogMainStack() {
  HANDLE process = GetCurrentProcess();
  // Collected while the main thread is suspended and logged after it resumes:
  // it may hold the log's lock.
  std::string lines;
  if (SuspendThread(g_main_thread) == DWORD(-1)) return;
  CONTEXT context = {};
  context.ContextFlags = CONTEXT_FULL;
  if (GetThreadContext(g_main_thread, &context)) {
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int depth = 0; depth < 40; ++depth) {
      if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, g_main_thread, &frame, &context, nullptr,
                       SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
          !frame.AddrPC.Offset) {
        break;
      }
      char buffer[sizeof(SYMBOL_INFO) + 256] = {};
      auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
      symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
      symbol->MaxNameLen = 255;
      DWORD64 displacement = 0;
      char module[MAX_PATH] = "?";
      HMODULE handle = nullptr;
      if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCSTR>(frame.AddrPC.Offset), &handle)) {
        GetModuleFileNameA(handle, module, MAX_PATH);
      }
      std::string name = SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)
                             ? symbol->Name
                             : "?";
      const char* base = std::strrchr(module, '\\');
      char line[512];
      std::snprintf(line, sizeof(line), "  #%d %s!%s+0x%llx\n", depth, base ? base + 1 : module,
                    name.c_str(), (unsigned long long)displacement);
      lines += line;
    }
  }
  ResumeThread(g_main_thread);
  Log("%s", lines.c_str());
}

void Watch() {
  bool reported = false, hung_reported = false;
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    int64_t stalled = NowMs() - g_last_tick_ms.load();
    // Windows' own view: "not responding" when the window's thread hasn't
    // taken its messages for a while.
    bool hung = g_window && IsHungAppWindow(g_window);
    if (hung && !hung_reported) {
      if (stalled < 0) {
        Log("watchdog: Windows reports the window not responding (in a dialog); main thread "
            "stack:");
      } else {
        Log("watchdog: Windows reports the window not responding (main loop last ran %lld ms "
            "ago); main thread stack:", (long long)stalled);
      }
      LogMainStack();
      hung_reported = true;
    } else if (!hung && hung_reported) {
      Log("watchdog: the window responds again");
      hung_reported = false;
    }
    if (stalled > 1500 && !reported) {
      Log("watchdog: the main loop has been stuck for %lld ms; main thread stack:",
          (long long)stalled);
      LogMainStack();
      reported = true;
    } else if (stalled < 1000 && reported) {
      Log("watchdog: the main loop runs again");
      reported = false;
    }
  }
}

}  // namespace

void StartWatchdog(void* window) {
  g_window = static_cast<HWND>(window);
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main_thread,
                  THREAD_ALL_ACCESS, FALSE, 0);
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(GetCurrentProcess(), nullptr, TRUE);
  WatchdogTick();
  std::thread(Watch).detach();
}

void WatchdogPause() { g_last_tick_ms.store(INT64_MAX / 2); }

void WatchdogTick() {
  g_last_tick_ms.store(NowMs());
}

}  // namespace launcher
