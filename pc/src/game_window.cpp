#include "game_window.h"

#include "data_folder.h"

#include <algorithm>
#include <filesystem>

#include <rex/cvar.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>

#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#endif

REXCVAR_DEFINE_BOOL(debug_keys, false, "Game",
                    "The SDK's overlays on their keys: F3 debug, F4 settings, F7 achievements, "
                    "` console (for developers; players don't get them)");

REXCVAR_DEFINE_INT32(windowed_width, 0, "Game",
                     "Windowed: the window's width in pixels (0 = as large as fits the screen). "
                     "Fitted to the screen if larger; kept 16:9.")
    .range(0, 8192);
REXCVAR_DEFINE_INT32(windowed_height, 0, "Game", "Windowed: the window's height (see windowed_width)")
    .range(0, 8192);

void RemoveDebugKeys() {
  if (REXCVAR_GET(debug_keys)) return;
  for (const char* bind : {"bind_debug_overlay", "bind_settings", "bind_achievements",
                           "bind_console"}) {
    rex::ui::UnregisterBind(bind);
  }
}

#ifdef _WIN32
namespace {

// The game's window: this process's SDL window (SDL is linked into the SDK's
// runtime DLL, so it's found by its window class, "SDL_app").
HWND FindGameWindow() {
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
  return search.found;
}

void SetIcon(HWND hwnd) {
  std::filesystem::path icon = DataFolder() / "icon.ico";
  std::error_code ec;
  if (!std::filesystem::exists(icon, ec)) return;
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
}

// The window's frame (title bar and borders) around a client area, in pixels.
SIZE FrameSize(HWND hwnd) {
  RECT rect = {0, 0, 0, 0};
  AdjustWindowRectExForDpi(&rect, DWORD(GetWindowLongW(hwnd, GWL_STYLE)), FALSE,
                           DWORD(GetWindowLongW(hwnd, GWL_EXSTYLE)), GetDpiForWindow(hwnd));
  return {rect.right - rect.left, rect.bottom - rect.top};
}

// Dragging an edge or corner keeps the client area 16:9: the edge being
// dragged decides, the other follows (as the launcher's window does).
LRESULT CALLBACK KeepAspect(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                            DWORD_PTR) {
  if (message == WM_SIZING) {
    auto* rect = reinterpret_cast<RECT*>(lparam);
    SIZE frame = FrameSize(hwnd);
    int client_w = rect->right - rect->left - frame.cx;
    int client_h = rect->bottom - rect->top - frame.cy;
    bool corner = wparam == WMSZ_TOPLEFT || wparam == WMSZ_TOPRIGHT ||
                  wparam == WMSZ_BOTTOMLEFT || wparam == WMSZ_BOTTOMRIGHT;
    bool width_leads = wparam == WMSZ_LEFT || wparam == WMSZ_RIGHT ||
                       (corner && client_w * 9 >= client_h * 16);
    if (width_leads) {
      client_h = client_w * 9 / 16;
      if (wparam == WMSZ_TOPLEFT || wparam == WMSZ_TOPRIGHT) {
        rect->top = rect->bottom - client_h - frame.cy;
      } else {
        rect->bottom = rect->top + client_h + frame.cy;
      }
    } else {
      client_w = client_h * 16 / 9;
      if (wparam == WMSZ_TOPLEFT || wparam == WMSZ_BOTTOMLEFT) {
        rect->left = rect->right - client_w - frame.cx;
      } else {
        rect->right = rect->left + client_w + frame.cx;
      }
    }
    return TRUE;
  }
  return DefSubclassProc(hwnd, message, wparam, lparam);
}

// Windowed: the configured size (or, unset, as large as fits), 16:9, made to fit
// the work area of the screen the window is on together with its frame, and
// centred there.
void SizeWindow(rex::ui::Window* window, HWND hwnd) {
  MONITORINFO monitor = {sizeof(monitor)};
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor)) return;
  const RECT& work = monitor.rcWork;
  int work_w = work.right - work.left, work_h = work.bottom - work.top;
  SIZE frame = FrameSize(hwnd);
  int max_w = work_w - frame.cx, max_h = work_h - frame.cy;
  int w = REXCVAR_GET(windowed_width), h = REXCVAR_GET(windowed_height);
  if (w <= 0 || h <= 0) {  // Automatic: the whole work area, then fitted below
    w = max_w;
    h = max_h;
  }
  // Fit, keeping 16:9.
  if (w * 9 > h * 16) w = h * 16 / 9;  // wider than 16:9 (Automatic on most screens)
  if (w > max_w) w = max_w, h = w * 9 / 16;
  if (h > max_h) h = max_h, w = h * 16 / 9;
  w = std::max(w, 640), h = std::max(h, 360);
  // The SDK keeps its own sizes in logical pixels (physical / display scale).
  float scale = float(GetDpiForWindow(hwnd)) / 96.0f;
  window->SetDesiredLogicalSize(uint32_t(w / scale + 0.5f), uint32_t(h / scale + 0.5f));
  RECT rect;
  GetWindowRect(hwnd, &rect);
  int outer_w = rect.right - rect.left, outer_h = rect.bottom - rect.top;
  SetWindowPos(hwnd, nullptr, work.left + (work_w - outer_w) / 2,
               work.top + std::max(0, (work_h - outer_h) / 2), 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace
#endif

void SetUpGameWindow(rex::ui::Window* window) {
  if (!window) return;
  window->SetTitle("2014 FIFA World Cup Brazil: Recompiled");
#ifdef _WIN32
  HWND hwnd = FindGameWindow();
  if (!hwnd) return;
  SetIcon(hwnd);
  SetWindowSubclass(hwnd, KeepAspect, 1, 0);
  if (!window->IsFullscreen()) SizeWindow(window, hwnd);
#endif
}
