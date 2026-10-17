#include "window_branding.h"

#include <filesystem>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>

#ifdef _WIN32
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(debug_keys, false, "Game",
                    "The SDK's overlays on their keys: F3 debug, F4 settings, F7 achievements, "
                    "` console (for developers; players don't get them)");

void RemoveDebugKeys() {
  if (REXCVAR_GET(debug_keys)) return;
  for (const char* bind : {"bind_debug_overlay", "bind_settings", "bind_achievements",
                           "bind_console"}) {
    rex::ui::UnregisterBind(bind);
  }
}

void ApplyWindowBranding(rex::ui::Window* window) {
  if (!window) return;
  window->SetTitle("2014 FIFA World Cup Brazil: Recompiled");
#ifdef _WIN32
  std::filesystem::path icon = rex::filesystem::GetExecutableFolder() / "icon.ico";
  std::error_code ec;
  if (!std::filesystem::exists(icon, ec)) return;
  // The game's window: this process's SDL window (SDL is linked into the
  // SDK's runtime DLL, so it's found by its window class, "SDL_app").
  struct Search {
    DWORD process;
    HWND found;
  } search = {GetCurrentProcessId(), nullptr};
  EnumWindows(
      [](HWND candidate, LPARAM param) -> BOOL {
        auto* s = reinterpret_cast<Search*>(param);
        DWORD process = 0;
        wchar_t name[32] = {};
        GetWindowThreadProcessId(candidate, &process);
        if (process != s->process || !GetClassNameW(candidate, name, 32) ||
            wcscmp(name, L"SDL_app") != 0) {
          return TRUE;
        }
        s->found = candidate;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&search));
  HWND hwnd = search.found;
  if (!hwnd) return;
  auto load = [&](int metric_x, int metric_y) {
    return static_cast<HICON>(LoadImageW(nullptr, icon.c_str(), IMAGE_ICON,
                                         GetSystemMetrics(metric_x), GetSystemMetrics(metric_y),
                                         LR_LOADFROMFILE));
  };
  if (HICON big = load(SM_CXICON, SM_CYICON)) {
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, LPARAM(big));
  }
  if (HICON little = load(SM_CXSMICON, SM_CYSMICON)) {
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, LPARAM(little));
  }
#endif
}
