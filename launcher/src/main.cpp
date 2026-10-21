// The launcher: a native Win32 + Direct3D 11 window drawing the game-styled
// menu (ui.cpp) with Dear ImGui, art and fonts read from the player's own game.
//
//   Launcher.exe [--game-folder <folder with fifawc14.exe>] [--screenshot <png>]
//                [--commands down,accept,...]
//
// The game folder defaults to the launcher's own folder; the game's data
// folder comes from fifawc14.toml there (game_data_root). --screenshot renders
// one 1920x1080 frame to a PNG and exits (for checking the design), after the
// menu commands in --commands (up, down, left, right, accept, back).

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <regex>
#include <sstream>
#include <string>

#include <imgui.h>

#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "audio.h"
#include "controllers.h"
#include "game_check.h"
#include "game_files.h"
#include "game_source.h"
#include "i18n.h"
#include "icon.h"
#include "textures.h"
#include "log.h"
#include "watchdog.h"
#include "settings.h"
#include "updater.h"
#include "about.h"
#include "setup.h"
#include "ui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

using Microsoft::WRL::ComPtr;
using launcher::T;
namespace fs = std::filesystem;

namespace {

// The project's name, on the window and its messages.
constexpr const wchar_t* kAppName = L"2014 FIFA World Cup Brazil: Recompiled";

ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain1> g_swap_chain;
ComPtr<ID3D11RenderTargetView> g_target;
launcher::Ui* g_ui = nullptr;
launcher::Setup* g_setup = nullptr;  // until the game is found
launcher::Audio* g_audio = nullptr;
HWND g_window = nullptr;
HANDLE g_game_process = nullptr;

// UTF-8 (the launcher's text) for Windows' own dialogs.
std::wstring Wide(const char* text) {
  int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
  std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
  if (length > 1) MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), length);
  return wide;
}

// Drops on the window, while the game is being found. Through OLE rather than
// WM_DROPFILES, so the setup screen also knows while something is dragged over
// it and lights its drop box up.
class DropTarget : public IDropTarget {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
      *object = static_cast<IDropTarget*>(this);
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  // It lives as long as the window: no counting.
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }

  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
    FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    files_ = g_setup && data->QueryGetData(&format) == S_OK;
    if (files_) g_setup->SetDragging(true);
    *effect = files_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
    *effect = files_ && g_setup ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragLeave() override {
    if (g_setup) g_setup->SetDragging(false);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
    *effect = DROPEFFECT_NONE;
    if (!g_setup) return S_OK;
    g_setup->SetDragging(false);
    FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    if (data->GetData(&format, &medium) != S_OK) return S_OK;
    auto drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
    wchar_t dropped[MAX_PATH * 4];
    if (drop && DragQueryFileW(drop, 0, dropped, UINT(std::size(dropped)))) {
      g_setup->Offer(dropped);
      SetForegroundWindow(g_window);
      *effect = DROPEFFECT_COPY;
    }
    if (drop) GlobalUnlock(medium.hGlobal);
    ReleaseStgMedium(&medium);
    return S_OK;
  }

 private:
  bool files_ = false;
};
DropTarget g_drop_target;

void CreateTarget() {
  ComPtr<ID3D11Texture2D> back_buffer;
  g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
  g_device->CreateRenderTargetView(back_buffer.Get(), nullptr, &g_target);
}

bool CreateDevice(HWND window) {
  UINT flags = 0;
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 1,
                               D3D11_SDK_VERSION, &g_device, nullptr, &g_context))) {
    return false;
  }
  if (!window) return true;
  ComPtr<IDXGIDevice> dxgi_device;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIFactory2> factory;
  g_device.As(&dxgi_device);
  dxgi_device->GetAdapter(&adapter);
  adapter->GetParent(IID_PPV_ARGS(&factory));
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  if (FAILED(factory->CreateSwapChainForHwnd(g_device.Get(), window, &desc, nullptr, nullptr,
                                             &g_swap_chain))) {
    return false;
  }
  factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
  CreateTarget();
  return true;
}

// The game's data folder from fifawc14.toml (game_data_root = "...").
fs::path GameDataRoot(const fs::path& data_folder) {
  std::ifstream config(data_folder / "fifawc14.toml");
  std::string line;
  std::regex pattern(R"(^\s*game_data_root\s*=\s*"([^"]*)\")");
  std::smatch match;
  while (std::getline(config, line)) {
    if (std::regex_search(line, match, pattern)) return fs::u8path(match[1].str());
  }
  return {};
}

// Starts fifawc14.exe from the game folder. If it can't, says why (for the menu).
std::string StartGame(const fs::path& game_folder) {
  if (g_game_process) return {};
  fs::path exe = game_folder / "fifawc14.exe";
  std::error_code ec;
  if (!fs::exists(exe, ec)) {
    launcher::Log("play: %s is missing", exe.u8string().c_str());
    return T("The game (fifawc14.exe) is missing from the launcher's folder. Your antivirus "
             "may have removed it. Restore it there, or download the release again.");
  }
  std::wstring command = L"\"" + exe.wstring() + L"\"";
  STARTUPINFOW startup = {sizeof(startup)};
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      game_folder.c_str(), &startup, &process)) {
    DWORD error = GetLastError();
    launcher::Log("play: starting %s failed, error %lu", exe.u8string().c_str(), error);
    return launcher::Tf("The game (fifawc14.exe) couldn't be started (Windows error {0}). Your "
                        "antivirus may be blocking it.",
                        {std::to_string(error)});
  }
  CloseHandle(process.hThread);
  g_game_process = process.hProcess;
  ShowWindow(g_window, SW_HIDE);
  return {};
}

// The game is built for processors with SSE4.2 (-march=x86-64-v2, pc/CMakeLists.txt):
// without it, it stops at its first such instruction. Asked of Windows rather
// than with cpuid, which antivirus heuristics count against a program.
bool ProcessorHasSse42() { return IsProcessorFeaturePresent(PF_SSE4_2_INSTRUCTIONS_AVAILABLE); }

// What to tell the player when the game ended with `code` (0 is a normal
// quit): why, if it's known, or the code and where its log is.
std::string GameExitMessage(DWORD code) {
  if (code == 0) return {};
  if (code == 0xC000001D && !ProcessorHasSse42()) {
    return T("The game stopped: your processor doesn't support SSE4.2, which the game needs "
             "(Intel from 2008 on, AMD from 2011).");
  }
  char text[16];
  std::snprintf(text, sizeof(text), "0x%08lX", static_cast<unsigned long>(code));
  return launcher::Tf("The game closed unexpectedly (error {0}). Please report it with "
                      "fifawc14.log (Settings, Data, Logs opens its folder).",
                      {text});
}

void ToggleFullscreen();

// A menu command to whichever screen is up.
void SendCommand(launcher::Command command, launcher::InputDevice device) {
  if (g_ui) {
    g_ui->OnCommand(command, device);
  } else if (g_setup) {
    g_setup->OnCommand(command);
  }
}

// A normal window without maximize: its size is kept to the launcher's shape
// (WM_SIZING), and F11 / Alt+Enter give real fullscreen.
constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam)) return true;
  switch (message) {
    case WM_SIZE:
      if (g_swap_chain && wparam != SIZE_MINIMIZED) {
        g_target.Reset();
        g_swap_chain->ResizeBuffers(0, LOWORD(lparam), HIWORD(lparam), DXGI_FORMAT_UNKNOWN, 0);
        CreateTarget();
      }
      return 0;
    case WM_GETMINMAXINFO: {
      // The launcher keeps its proportions between 990x630 and 1760x1120 (client).
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      RECT min_rect = {0, 0, 990, 630}, max_rect = {0, 0, 1760, 1120};
      AdjustWindowRect(&min_rect, kWindowStyle, FALSE);
      AdjustWindowRect(&max_rect, kWindowStyle, FALSE);
      info->ptMinTrackSize = {min_rect.right - min_rect.left, min_rect.bottom - min_rect.top};
      info->ptMaxTrackSize = {max_rect.right - max_rect.left, max_rect.bottom - max_rect.top};
      return 0;
    }
    case WM_SIZING: {
      // Resizing keeps 11:7: the edge being dragged decides, the other follows.
      auto* rect = reinterpret_cast<RECT*>(lparam);
      RECT frame = {0, 0, 0, 0};
      AdjustWindowRect(&frame, kWindowStyle, FALSE);
      int extra_w = frame.right - frame.left, extra_h = frame.bottom - frame.top;
      int client_w = rect->right - rect->left - extra_w;
      int client_h = rect->bottom - rect->top - extra_h;
      bool width_leads = wparam == WMSZ_LEFT || wparam == WMSZ_RIGHT ||
                         ((wparam == WMSZ_TOPLEFT || wparam == WMSZ_TOPRIGHT ||
                           wparam == WMSZ_BOTTOMLEFT || wparam == WMSZ_BOTTOMRIGHT) &&
                          client_w * 7 >= client_h * 11);
      if (width_leads) {
        client_h = client_w * 7 / 11;
        if (wparam == WMSZ_TOPLEFT || wparam == WMSZ_TOPRIGHT) {
          rect->top = rect->bottom - client_h - extra_h;
        } else {
          rect->bottom = rect->top + client_h + extra_h;
        }
      } else {
        client_w = client_h * 11 / 7;
        if (wparam == WMSZ_TOPLEFT || wparam == WMSZ_BOTTOMLEFT) {
          rect->left = rect->right - client_w - extra_w;
        } else {
          rect->right = rect->left + client_w + extra_w;
        }
      }
      return TRUE;
    }
    case WM_ACTIVATEAPP:
      launcher::Log("window %s", wparam ? "activated" : "deactivated");
      break;
    case WM_SYSKEYDOWN:
      if (wparam == VK_RETURN && (lparam & (1 << 29))) {
        ToggleFullscreen();
        return 0;
      }
      break;
    case WM_KEYDOWN:
      if (wparam == VK_F11) {
        ToggleFullscreen();
        return 0;
      }
      {
        constexpr auto kKeys = launcher::InputDevice::kKeyboard;
        switch (wparam) {
          case VK_UP: case 'W': SendCommand(launcher::Command::kUp, kKeys); break;
          case VK_DOWN: case 'S': SendCommand(launcher::Command::kDown, kKeys); break;
          case VK_LEFT: case 'A': SendCommand(launcher::Command::kLeft, kKeys); break;
          case VK_RIGHT: case 'D': SendCommand(launcher::Command::kRight, kKeys); break;
          case VK_RETURN: case VK_SPACE: SendCommand(launcher::Command::kAccept, kKeys); break;
          case VK_ESCAPE: case VK_BACK: SendCommand(launcher::Command::kBack, kKeys); break;
          case 'N': if (g_ui) g_ui->SkipSong(); break;
        }
      }
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

// Controller buttons (read on their own thread, controllers.cpp) as menu
// commands: d-pad or left stick to move, with repeat when held; South (A /
// Cross) selects, East (B / Circle) goes back, North (Y / Triangle) skips the song.
launcher::Controllers g_controllers;

void PollController(float dt) {
  using launcher::Controllers;
  enum : uint32_t {
    kUp = Controllers::kUp, kDown = Controllers::kDown, kLeft = Controllers::kLeft,
    kRight = Controllers::kRight, kSouth = Controllers::kSouth, kEast = Controllers::kEast,
    kNorth = Controllers::kNorth
  };
  static uint32_t previous = 0;
  static float repeat = 0;
  uint32_t buttons = g_controllers.Buttons();
  if (g_ui) {
    g_ui->SetControllerStyle(g_controllers.PlayStation() ? launcher::ControllerStyle::kPlayStation
                                                         : launcher::ControllerStyle::kXbox);
  }
  uint32_t pressed = buttons & ~previous;
  const uint32_t directions = kUp | kDown | kLeft | kRight;
  if (buttons & directions & previous) {
    repeat -= dt;
    if (repeat <= 0) {
      pressed |= buttons & directions;
      repeat = 0.12f;
    }
  } else {
    repeat = 0.4f;
  }
  previous = buttons;
  constexpr auto kPad = launcher::InputDevice::kController;
  if (pressed & kUp) SendCommand(launcher::Command::kUp, kPad);
  if (pressed & kDown) SendCommand(launcher::Command::kDown, kPad);
  if (pressed & kLeft) SendCommand(launcher::Command::kLeft, kPad);
  if (pressed & kRight) SendCommand(launcher::Command::kRight, kPad);
  if (pressed & kSouth) SendCommand(launcher::Command::kAccept, kPad);
  if (pressed & kEast) SendCommand(launcher::Command::kBack, kPad);
  if ((pressed & kNorth) && g_ui) g_ui->SkipSong();
}

// fifawc14.toml: created if missing, and given the settings the game needs to
// run correctly if they're absent (the player's own choices are kept).
void EnsureGameConfig(const fs::path& data_folder) {
  fs::path path = data_folder / "fifawc14.toml";
  bool created = !fs::exists(path);
  launcher::GameConfig config(path);
  auto require = [&](const char* key, const char* value) {
    if (config.Get(key, "").empty()) config.Set(key, value);
  };
  require("gpu_plugin", "\"xenos\"");
  require("gpu_allow_invalid_fetch_constants", "true");
  require("readback_resolve", "\"none\"");
  require("protect_zero", "false");
  require("execute_unclipped_draw_vs_on_cpu", "true");
  // A draw whose pipeline is still compiling waits for it instead of being
  // skipped. Skipped, a texture the game renders once (the ground outside the
  // stadium) stayed black for the whole first run, before the shader cache had it.
  require("async_shader_compilation", "false");
  // One submission to the GPU per frame instead of one per command buffer:
  // each ExecuteCommandLists waits while the GPU is busy, and the game waits on
  // the emulation meanwhile, so at 4K frames that fit missed the refresh.
  // Measured 2026-10-20: 4K play 3-5 -> 0-1 ms in ExecuteCommandLists, the
  // stutters during play mostly gone.
  require("d3d12_submit_on_primary_buffer_end", "false");
  // No FXAA (the SDK's swap_post_effect): measured 2026-10-17, the game already
  // smooths its 3D edges (MSAA), so FXAA only softened text and HUD edges. The
  // launcher had it as an option; a config that still has it is set back.
  if (config.Get("swap_post_effect", "\"none\"") != "\"none\"") {
    config.Set("swap_post_effect", "\"none\"");
  }
  // The window's size moved from the SDK's window_width / window_height, which
  // also set the video mode the game is told about, to the game's own
  // windowed_width / windowed_height (pc/src/game_window.h).
  for (const char* axis : {"width", "height"}) {
    std::string old = config.Get(std::string("window_") + axis, "0");
    if (old != "0" && !old.empty()) {
      config.Set(std::string("windowed_") + axis, old);
      config.Set(std::string("window_") + axis, "0");
    }
  }
  // The keyboard always works next to a controller (the SDK merges them), with
  // the launcher's layout; keybinds already in the file are kept.
  for (const auto& [key, value] : launcher::KeyboardSettings()) {
    if (key == "mnk_mode" || key == "mnk_exact_modifiers") {
      config.Set(key, value);
    } else {
      require(key.c_str(), value.c_str());
    }
  }
  if (created) {
    // A new install's defaults: 60 FPS everywhere, 16x texture filtering, a
    // modest log. Resolution stays the console's 720p (1440p and 4K cost a lot
    // in cutscenes so far) and variable refresh stays on (the SDK's defaults):
    // off, every frame waits for the screen's refresh, which beats against the
    // game's own 60 Hz pacing and drops it to 30-45 FPS (measured 2026-10-16).
    config.Set("present_interval_cap", "1");
    config.Set("frame_rate_floor", "60");
    config.Set("anisotropic_override", "5");
    config.Set("log_file", "\"fifawc14.log\"");
    config.Set("log_level", "\"info\"");
  }
  config.Save();
}

// The folder or .iso picker, over the launcher window. Empty if cancelled.
fs::path PickLocation(bool iso) {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog)))) {
    return {};
  }
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (iso ? FOS_FILEMUSTEXIST : FOS_PICKFOLDERS));
  if (iso) {
    std::wstring images = Wide(T("Disc images (*.iso)")), all = Wide(T("All files"));
    COMDLG_FILTERSPEC types[] = {{images.c_str(), L"*.iso"}, {all.c_str(), L"*.*"}};
    dialog->SetFileTypes(2, types);
    dialog->SetTitle(Wide(T("The game's disc image")).c_str());
  } else {
    dialog->SetTitle(Wide(T("The game's folder (with default.xex)")).c_str());
  }
  ComPtr<IShellItem> item;
  PWSTR picked = nullptr;
  launcher::WatchdogPause();
  if (FAILED(dialog->Show(g_window)) || FAILED(dialog->GetResult(&item)) ||
      FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &picked))) {
    return {};
  }
  fs::path location = picked;
  CoTaskMemFree(picked);
  return location;
}

// The game's folder or .iso, written to fifawc14.toml (game_data_root).
void SaveGameLocation(const fs::path& data_folder, const fs::path& location) {
  launcher::GameConfig config(data_folder / "fifawc14.toml");
  std::u8string utf8 = location.generic_u8string();
  config.Set("game_data_root",
             "\"" + std::string(reinterpret_cast<const char*>(utf8.c_str())) + "\"");
  config.Save();
}

// The launcher window's size and position, kept in launcher.toml.
void SaveWindowPlacement(const fs::path& data_folder) {
  WINDOWPLACEMENT placement = {sizeof(placement)};
  if (!GetWindowPlacement(g_window, &placement)) return;
  launcher::GameConfig config(data_folder / "launcher.toml");
  const RECT& r = placement.rcNormalPosition;
  config.Set("window_x", std::to_string(r.left));
  config.Set("window_y", std::to_string(r.top));
  config.Set("window_width", std::to_string(r.right - r.left));
  config.Set("window_height", std::to_string(r.bottom - r.top));
  config.Save();
}

// F11 / Alt+Enter: the launcher borderless over its monitor, or back.
void ToggleFullscreen() {
  static WINDOWPLACEMENT previous = {sizeof(previous)};
  LONG style = GetWindowLongW(g_window, GWL_STYLE);
  if (style & WS_CAPTION) {
    MONITORINFO monitor = {sizeof(monitor)};
    GetWindowPlacement(g_window, &previous);
    GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    SetWindowLongW(g_window, GWL_STYLE, style & ~kWindowStyle);
    const RECT& r = monitor.rcMonitor;
    SetWindowPos(g_window, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  } else {
    SetWindowLongW(g_window, GWL_STYLE, style | kWindowStyle);
    SetWindowPlacement(g_window, &previous);
    SetWindowPos(g_window, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  }
}

// The game's saves and caches: Documents\2014 FIFA World Cup Brazil, as the
// game sets it (pc/src/fifawc14_app.h). Earlier builds used Documents\fifawc14:
// moved over once, here or by the game, whichever starts first.
fs::path UserFolder() {
  PWSTR documents = nullptr;
  fs::path folder;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) {
    folder = fs::path(documents) / "2014 FIFA World Cup Brazil";
    fs::path old = fs::path(documents) / "fifawc14";
    std::error_code ec;
    if (!fs::exists(folder, ec) && fs::is_directory(old, ec)) {
      fs::rename(old, folder, ec);
      launcher::Log("saves moved to %s%s", folder.u8string().c_str(), ec ? " (failed)" : "");
    }
  }
  CoTaskMemFree(documents);
  return folder;
}

// Where the launcher and the game keep their settings, logs, icon and font
// copy: next to them in the portable version (portable.txt is there) or a
// developer build (a fifawc14.toml is there), otherwise the user folder above.
// The game uses the same rule (pc/src/data_folder.h). An installed copy can then
// sit in Program Files, which programs can't write to.
fs::path DataFolder(const fs::path& game_folder) {
  std::error_code ec;
  if (fs::exists(game_folder / "portable.txt", ec) ||
      fs::exists(game_folder / "fifawc14.toml", ec)) {
    return game_folder;
  }
  fs::path folder = UserFolder();
  fs::create_directories(folder, ec);
  return folder;
}

// Where the game keeps its saves and shader cache: a "saves" folder in the
// portable version's own folder (it keeps everything there), otherwise the user
// folder above. The game uses the same rule (pc/src/data_folder.h, SavesFolder).
fs::path SavesFolder(const fs::path& game_folder) {
  std::error_code ec;
  if (fs::exists(game_folder / "portable.txt", ec)) return game_folder / "saves";
  return UserFolder();
}

// "down,accept" -> commands, for screenshots of other screens.
void RunCommands(const std::function<void(launcher::Command)>& send, const std::wstring& list) {
  std::wstringstream stream(list);
  std::wstring name;
  while (std::getline(stream, name, L',')) {
    launcher::Command command = name == L"up"       ? launcher::Command::kUp
                                : name == L"down"   ? launcher::Command::kDown
                                : name == L"left"   ? launcher::Command::kLeft
                                : name == L"right"  ? launcher::Command::kRight
                                : name == L"accept" ? launcher::Command::kAccept
                                : name == L"back"   ? launcher::Command::kBack
                                                    : launcher::Command::kNone;
    send(command);
  }
}

// Renders one frame offscreen at 1100x700 (the default window) and writes it as a PNG.
// 32-bit BGRA rows (top to bottom, `stride` bytes apart) as a PNG, with WIC.
bool WritePng(const fs::path& png, UINT width, UINT height, UINT stride, const BYTE* bgra) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ComPtr<IWICImagingFactory> wic;
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> frame;
  bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&wic))) &&
            SUCCEEDED(wic->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromFilename(png.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
            SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(width, height));
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  return ok && SUCCEEDED(frame->SetPixelFormat(&format)) &&
         SUCCEEDED(frame->WritePixels(height, stride, stride * height, const_cast<BYTE*>(bgra))) &&
         SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

// An RGBA image (rows top to bottom, as DecodeDds gives) as a PNG.
bool WriteRgbaPng(const fs::path& png, const std::vector<uint8_t>& rgba, int width, int height) {
  std::vector<uint8_t> bgra(rgba);
  for (size_t i = 0; i + 3 < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
  return WritePng(png, UINT(width), UINT(height), UINT(width) * 4, bgra.data());
}

bool Screenshot(const std::function<void(float, float, float)>& frame_ui, const fs::path& png) {
  const UINT width = 1100, height = 700;
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // what WIC's PNG encoder takes as is
  desc.SampleDesc.Count = 1;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  g_device->CreateTexture2D(&desc, nullptr, &target);
  g_device->CreateRenderTargetView(target.Get(), nullptr, &g_target);
  ImGui::GetIO().DisplaySize = ImVec2(float(width), float(height));
  // A few frames so the entry animations settle.
  for (int i = 0; i < 30; ++i) {
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    frame_ui(float(width), float(height), 1.0f / 30.0f);
    ImGui::Render();
    const float clear[4] = {0, 0, 0, 1};
    g_context->OMSetRenderTargets(1, g_target.GetAddressOf(), nullptr);
    g_context->ClearRenderTargetView(g_target.Get(), clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  }
  desc.BindFlags = 0;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  g_device->CreateTexture2D(&desc, nullptr, &staging);
  g_context->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped;
  if (FAILED(g_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

  bool ok = WritePng(png, width, height, mapped.RowPitch, static_cast<const BYTE*>(mapped.pData));
  g_context->Unmap(staging.Get(), 0);
  return ok;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  wchar_t exe_path[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  fs::path game_folder = fs::path(exe_path).parent_path();
  fs::path screenshot;
  std::wstring commands;
  fs::path audio_report;
  fs::path offer;
  fs::path icon_test;
  // Where to look for updates: the project's releases, or a test feed.
  std::string update_feed = launcher::kUpdateFeed;
  bool update_feed_given = false;
  for (int i = 1; i + 1 < argc; ++i) {
    std::wstring arg = argv[i];
    if (arg == L"--game-folder") game_folder = argv[++i];
    else if (arg == L"--screenshot") screenshot = argv[++i];
    else if (arg == L"--commands") commands = argv[++i];
    else if (arg == L"--audio-report") audio_report = argv[++i];
    else if (arg == L"--offer") offer = argv[++i];
    else if (arg == L"--icon") icon_test = argv[++i];
    else if (arg == L"--update-feed") {
      std::u8string feed = fs::path(argv[++i]).u8string();
      update_feed.assign(feed.begin(), feed.end());
      update_feed_given = true;
    }
  }

  fs::path data_folder = DataFolder(game_folder);

  // COM for XAudio2, the image encoder and the folder / .iso pickers, which
  // need a single-threaded apartment (in a multithreaded one they hang); OLE
  // (in that apartment) for drag and drop.
  OleInitialize(nullptr);
  launcher::OpenLog(data_folder / "launcher.log");
  launcher::Log("game folder: %s, data: %s", game_folder.u8string().c_str(),
                data_folder.u8string().c_str());
  launcher::CleanUpAfterUpdate(game_folder);  // what a portable update left behind

  EnsureGameConfig(data_folder);
  // The language chosen before, or Windows' region setting's until one is.
  {
    launcher::GameConfig config(data_folder / "launcher.toml");
    std::string code = config.Get("language", "");
    if (code.size() >= 2 && code.front() == '"') code = code.substr(1, code.size() - 2);
    launcher::SetLanguage(launcher::LanguageFromCode(code, launcher::SystemLanguage()));
  }
  // The game's folder or .iso from last time, if it's still the right game;
  // otherwise the setup screen finds it. --offer <path> shows the setup
  // screen's answer for that path (with --screenshot, to check its messages).
  std::unique_ptr<launcher::GameSource> source;
  std::vector<uint32_t> game_languages;  // the disc's languages (GameCheck::languages)
  if (offer.empty()) {
    fs::path data_root = GameDataRoot(data_folder);
    if (!data_root.empty()) source = launcher::OpenGameSource(data_root);
    if (source) {
      launcher::GameCheck check = launcher::CheckGame(*source);
      if (check.result != launcher::GameCheck::Result::kFound) {
        launcher::Log("the game at '%s' doesn't check out (result %d), asking",
                      data_root.u8string().c_str(), int(check.result));
        source.reset();
      }
      game_languages = check.languages;
    } else {
      launcher::Log("no game at '%s', asking", data_root.u8string().c_str());
    }
  }
  if (!audio_report.empty()) {
    // Decodes the menu bank and the first songs, and writes what came out.
    if (!source) return 1;
    launcher::GameFiles files(*source);
    std::ofstream report(audio_report);
    const char* banks[] = {"audiodata/uisfx/ui_sfx_core_menu", "audiodata/eatrax/eatrax_01",
                           "audiodata/eatrax/eatrax_02"};
    for (const char* name : banks) {
      launcher::SoundBank bank;
      bool loaded = bank.Load(files, name);
      report << name << ": " << (loaded ? bank.entries().size() : 0) << " sounds" << std::endl;
      for (size_t i = 0; loaded && i < bank.entries().size() && i < 6; ++i) {
        auto start = std::chrono::steady_clock::now();
        launcher::Pcm pcm;
        bool ok = launcher::DecodeSps(bank.Stream(files, i), pcm);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        size_t frames = pcm.channels ? pcm.samples.size() / pcm.channels : 0;
        int peak = 0;
        for (int16_t v : pcm.samples) peak = std::max(peak, std::abs(int(v)));
        report << "  " << bank.entries()[i].name << ": " << (ok ? "ok" : "FAILED") << ", "
               << pcm.sample_rate << " Hz x" << pcm.channels << ", "
               << (pcm.sample_rate ? double(frames) / pcm.sample_rate : 0) << " s, peak " << peak
               << ", decoded in " << ms << " ms" << std::endl;
      }
    }
    return 0;
  }

  if (!icon_test.empty()) {  // writes the icon from the game's art there, and exits
    if (!source) return 1;
    launcher::GameFiles files(*source);
    return launcher::WriteGameIcon(files, icon_test) ? 0 : 2;
  }

  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;

  if (!screenshot.empty()) {
    if (!CreateDevice(nullptr)) return 1;
    ImGui_ImplDX11_Init(g_device.Get(), g_context.Get());
    if (!source) {
      launcher::Setup setup;
      if (!setup.Load()) return 2;
      if (!offer.empty()) setup.Offer(offer);
      RunCommands([&](launcher::Command command) { setup.OnCommand(command); }, commands);
      return Screenshot([&](float w, float h, float dt) { setup.Frame(w, h, dt); }, screenshot)
                 ? 0 : 3;
    }
    launcher::GameFiles files(*source);
    launcher::Ui ui;
    if (!ui.Load(g_device.Get(), files, data_folder, SavesFolder(game_folder))) return 2;
    // Play shows what it would say if the game can't start (it never starts
    // here); Settings, Data, Game shows the setup screen it opens.
    bool change_game = false;
    ui.on_play = [&] {
      if (!fs::exists(game_folder / "fifawc14.exe")) ui.ShowMessage(StartGame(game_folder));
    };
    ui.on_change_game = [&] { change_game = true; };
    RunCommands([&](launcher::Command command) {
      ui.OnCommand(command, launcher::InputDevice::kKeyboard);
    }, commands);
    if (change_game) {
      launcher::Setup setup;
      if (!setup.Load()) return 2;
      setup.AllowBack(source->location());
      return Screenshot([&](float w, float h, float dt) { setup.Frame(w, h, dt); }, screenshot)
                 ? 0 : 3;
    }
    return Screenshot([&](float w, float h, float dt) { ui.Frame(w, h, dt); }, screenshot) ? 0 : 3;
  }

  WNDCLASSEXW window_class = {sizeof(window_class)};
  window_class.style = CS_CLASSDC;
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
  window_class.lpszClassName = L"Fifawc14Launcher";
  RegisterClassExW(&window_class);
  // 1100x700 (client), or the size and position from last time (launcher.toml)
  // if they're the launcher's shape.
  launcher::GameConfig placement(data_folder / "launcher.toml");
  RECT default_rect = {0, 0, 1100, 700};
  AdjustWindowRect(&default_rect, kWindowStyle, FALSE);
  int win_w = default_rect.right - default_rect.left, win_h = default_rect.bottom - default_rect.top;
  int win_x = CW_USEDEFAULT, win_y = CW_USEDEFAULT;
  int saved_w = std::stoi(placement.Get("window_width", "0"));
  int saved_h = std::stoi(placement.Get("window_height", "0"));
  int extra_w = win_w - 1100, extra_h = win_h - 700;
  if (saved_w > 0 && std::abs((saved_w - extra_w) * 7 - (saved_h - extra_h) * 11) < 40) {
    win_w = saved_w;
    win_h = saved_h;
    win_x = std::stoi(placement.Get("window_x", std::to_string(CW_USEDEFAULT)));
    win_y = std::stoi(placement.Get("window_y", std::to_string(CW_USEDEFAULT)));
  }
  g_window = CreateWindowExW(0, window_class.lpszClassName, kAppName,
                             kWindowStyle, win_x, win_y, win_w, win_h, nullptr, nullptr, instance,
                             nullptr);
  if (!CreateDevice(g_window)) {
    launcher::Log("no Direct3D 11 device");
    return 1;
  }
  g_controllers.Start();
  ImGui_ImplWin32_Init(g_window);
  ImGui_ImplDX11_Init(g_device.Get(), g_context.Get());

  RegisterDragDrop(g_window, &g_drop_target);

  // The menu, once the game is found (declared in this order so the music,
  // which streams from the game's files, stops before they close).
  std::unique_ptr<launcher::GameFiles> files;
  std::unique_ptr<launcher::Ui> ui;
  std::unique_ptr<launcher::Audio> audio;
  bool change_game = false;  // Settings, Data, Game pressed: handled after the frame
  // Updates (updater.h): checked once, in the background, from the start; the
  // offer shows once the menu is up (the UI holds it back during the first
  // start's welcome). Not for developer builds unless a test feed is given.
  launcher::InstallKind install_kind = launcher::DetectInstallKind(game_folder);
  std::future<std::optional<launcher::Release>> update_check;
  std::optional<launcher::Release> update_release;
  std::unique_ptr<launcher::UpdateJob> update_job;
  {
    launcher::GameConfig settings(data_folder / "launcher.toml");
    bool wanted = settings.Get("check_updates", "true") != "false";
    if (update_feed_given || (wanted && install_kind != launcher::InstallKind::kDeveloper)) {
      update_check = std::async(std::launch::async, launcher::CheckForUpdate, update_feed,
                                std::string(launcher::LauncherVersion()));
    }
  }
  auto load_menu = [&]() -> bool {
    files = std::make_unique<launcher::GameFiles>(*source);
    launcher::Log("game data: %s, %zu files in its archives",
                  source->location().u8string().c_str(), files->entry_count());
    // A newcomer's first start opens with the tournament's song. Noted before
    // the UI reads launcher.toml, which it writes back later.
    bool anthem_first;
    {
      launcher::GameConfig first_start(data_folder / "launcher.toml");
      anthem_first = first_start.Get("anthem_played", "false") != "true";
      if (anthem_first) {
        first_start.Set("anthem_played", "true");
        first_start.Save();
      }
    }
    // The icon from the game's art, made once, and again if it's from another
    // picture than the current choice (see icon.h). Noted in launcher.toml
    // before the UI reads it, as above.
    fs::path icon = launcher::IconPath(data_folder);
    std::error_code ec;
    {
      launcher::GameConfig made(data_folder / "launcher.toml");
      std::string source = std::string("\"") + launcher::IconSource() + "\"";
      if ((!fs::exists(icon, ec) || made.Get("icon_source", "") != source) &&
          launcher::WriteGameIcon(*files, icon)) {
        made.Set("icon_source", source);
        made.Save();
        launcher::RefreshShellIcons();
      }
    }
    ui = std::make_unique<launcher::Ui>();
    if (!ui->Load(g_device.Get(), *files, data_folder, SavesFolder(game_folder))) {
      MessageBoxW(g_window, Wide(T("The menu's art couldn't be read from the game's files.")).c_str(),
                  kAppName, MB_ICONERROR);
      return false;
    }
    audio = std::make_unique<launcher::Audio>();
    audio->Load(*files, anthem_first);
    g_audio = audio.get();
    ui->SetAudio(audio.get());
    // The game has its own sound: the menu's music stops while it runs.
    ui->on_play = [&] {
      audio->SetMusic(false, 0.0f);
      // The quit shortcut's button icons (pc/src/quit_shortcut.cpp) match the
      // controller in use.
      launcher::GameConfig config(data_folder / "fifawc14.toml");
      config.Set("quit_buttons", g_controllers.PlayStation() ? "\"playstation\"" : "\"xbox\"");
      // The launcher's language for the game too: its language screen at
      // start has it selected (the console's language setting).
      // Only one the disc has: asked for another, the game picks its own (the
      // European disc German), so English then.
      uint32_t language = launcher::Info(launcher::CurrentLanguage()).xbox;
      if (!game_languages.empty() &&
          std::find(game_languages.begin(), game_languages.end(), language) == game_languages.end()) {
        language = 1;
      }
      config.Set("user_language", std::to_string(language));
      // The game's quit shortcut (pc/src/quit_shortcut.cpp) in the launcher's language.
      config.Set("quit_language", std::string("\"") + launcher::Info(launcher::CurrentLanguage()).code + "\"");
      config.Save();
      std::string problem = StartGame(game_folder);
      if (!problem.empty()) {
        ui->ApplySoundSettings();  // the music back
        ui->ShowMessage(problem);
      }
    };
    ui->on_quit = [] { PostQuitMessage(0); };
    ui->on_change_game = [&] { change_game = true; };
    ui->on_update = [&] {
      update_job.reset();  // a failed one, if any
      update_job = std::make_unique<launcher::UpdateJob>(*update_release, install_kind, game_folder);
      ui->SetUpdateJob(update_job.get());
    };
    // A menu made again (another game chosen) shows a running update again.
    if (update_job) {
      ui->ShowUpdate(*update_release);
      ui->SetUpdateJob(update_job.get());
    }
    g_ui = ui.get();
    if (!ProcessorHasSse42()) {
      launcher::Log("processor without SSE4.2");
      ui->ShowMessage(T("Your processor doesn't support SSE4.2, which the game needs (Intel "
                        "from 2008 on, AMD from 2011). The game won't start on this computer."));
    }
    // The game's condensed font, for the game's frame-rate display (pc/src/fps_counter.cpp).
    fs::path font = data_folder / "futurastd-condensed.ttf";
    if (!fs::exists(font, ec)) {
      if (auto data = files->Read("data/ui/fonts/futurastd-condensed.ttf")) {
        std::ofstream(font, std::ios::binary)
            .write(reinterpret_cast<const char*>(data->data()), std::streamsize(data->size()));
      }
    }
    // One of the game's brush-stroke panels, behind the game's quit shortcut
    // (pc/src/quit_shortcut.cpp).
    fs::path panel = data_folder / "quit-panel.png";
    if (!fs::exists(panel, ec)) {
      if (auto dds = files->Read("data/ui/imgassets/custompanels/custompanels_42.dds")) {
        int width = 0, height = 0;
        std::vector<uint8_t> rgba = launcher::DecodeDds(*dds, &width, &height);
        if (!rgba.empty()) WriteRgbaPng(panel, rgba, width, height);
      }
    }
    launcher::SetWindowIcon(g_window, icon);
    return true;
  };

  // Until then, the setup screen. Its buttons open the pickers after the
  // frame (they're modal), and Continue fades it out and the menu in.
  std::unique_ptr<launcher::Setup> setup;
  std::unique_ptr<launcher::GameSource> chosen;  // Continue's game, until the fade ends
  int pick = 0;          // 1: a folder, 2: an .iso, asked for after this frame
  bool leaving = false;  // Continue pressed: the setup screen fading out
  bool going_back = false;  // Back pressed (changing the game from Settings)
  float black = 0.0f;    // 0..1, black over the window between the two
  auto open_setup = [&]() -> bool {
    setup = std::make_unique<launcher::Setup>();
    if (!setup->Load()) {
      MessageBoxW(g_window, Wide(T("Windows' Segoe UI font is missing.")).c_str(), kAppName,
                  MB_ICONERROR);
      return false;
    }
    setup->on_choose_folder = [&] { pick = 1; };
    setup->on_choose_iso = [&] { pick = 2; };
    setup->on_continue = [&](std::unique_ptr<launcher::GameSource> found) {
      chosen = std::move(found);
      game_languages = setup->check().languages;
      leaving = true;
    };
    setup->on_back = [&] { going_back = true; };
    setup->on_language = [&] {
      launcher::GameConfig config(data_folder / "launcher.toml");
      config.Set("language", std::string("\"") + launcher::Info(launcher::CurrentLanguage()).code + "\"");
      config.Save();
    };
    if (source) setup->AllowBack(source->location());
    g_setup = setup.get();
    g_ui = nullptr;
    return true;
  };
  if (source) {
    if (!load_menu()) return 2;
  } else {
    if (!open_setup()) return 2;
    if (!offer.empty()) setup->Offer(offer);
  }
  ShowWindow(g_window, SW_SHOWDEFAULT);

  auto last = std::chrono::steady_clock::now();
  bool running = true;
  launcher::StartWatchdog(g_window);
  // Time spent per part of a frame, logged for frames over 0.2 s.
  auto clock = [] { return std::chrono::steady_clock::now(); };
  auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  while (running) {
    launcher::WatchdogTick();
    auto t_start = clock();
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
      if (message.message == WM_QUIT) running = false;
    }
    if (!running) break;
    // While the game runs the launcher stays hidden, and comes back when it exits.
    if (g_game_process) {
      if (WaitForSingleObject(g_game_process, 100) == WAIT_OBJECT_0) {
        DWORD exit_code = 0;
        GetExitCodeProcess(g_game_process, &exit_code);
        CloseHandle(g_game_process);
        g_game_process = nullptr;
        ShowWindow(g_window, SW_SHOW);
        SetForegroundWindow(g_window);
        // Play was pressed on the mouse button's press, and the window hidden
        // before its release: the menu would still think it's held, and take
        // the next click for its release.
        for (int button = 0; button < ImGuiMouseButton_COUNT; ++button) {
          ImGui::GetIO().AddMouseButtonEvent(button, false);
        }
        ui->ApplySoundSettings();
        // A crash looks like the menu coming back by itself: say what happened.
        launcher::Log("game exited with code 0x%08lX", static_cast<unsigned long>(exit_code));
        if (std::string message = GameExitMessage(exit_code); !message.empty()) {
          ui->ShowMessage(message);
        }
      }
      continue;
    }
    auto now = std::chrono::steady_clock::now();
    float frame_seconds = std::chrono::duration<float>(now - last).count();
    float dt = std::min(frame_seconds, 0.1f);
    last = now;
    auto t_messages = clock();
    PollController(dt);
    auto t_controller = clock();
    if (audio) audio->Update();
    auto t_audio = clock();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    RECT client;
    GetClientRect(g_window, &client);
    if (setup) {
      setup->Frame(float(client.right), float(client.bottom), dt);
    } else {
      ui->Frame(float(client.right), float(client.bottom), dt);
    }
    if (black > 0) {
      ImGui::GetForegroundDrawList()->AddRectFilled(
          ImVec2(0, 0), ImVec2(float(client.right), float(client.bottom)),
          IM_COL32(0, 0, 0, int(255 * std::min(black, 1.0f))));
    }
    ImGui::Render();
    auto t_ui = clock();
    const float clear[4] = {0, 0, 0, 1};
    g_context->OMSetRenderTargets(1, g_target.GetAddressOf(), nullptr);
    g_context->ClearRenderTargetView(g_target.Get(), clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    auto t_draw = clock();
    g_swap_chain->Present(1, 0);
    auto t_end = clock();
    // A portable update's *.old files: the old launcher may still have held
    // them at start, so once more a few seconds in.
    static auto started = std::chrono::steady_clock::now();
    static bool cleaned = false;
    if (!cleaned && std::chrono::steady_clock::now() - started > std::chrono::seconds(5)) {
      cleaned = true;
      launcher::CleanUpAfterUpdate(game_folder);
    }
    // The update check's answer, once the menu is there to show it.
    if (update_check.valid() && ui &&
        update_check.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      update_release = update_check.get();
      if (update_release) ui->ShowUpdate(*update_release);
    }
    // Downloaded and checked: start the installer (or the new portable
    // launcher) and close, so the files can be replaced.
    if (update_job && update_job->state() == launcher::UpdateJob::State::kReady &&
        update_job->Finish()) {
      PostQuitMessage(0);
    }
    if (change_game) {  // Settings, Data, Game: the setup screen over the menu
      change_game = false;
      if (!open_setup()) return 2;
    }
    if (setup) {
      if (pick) {
        fs::path picked = PickLocation(pick == 2);
        pick = 0;
        if (!picked.empty()) setup->Offer(picked);
        last = std::chrono::steady_clock::now();
      }
      if (going_back) {  // the menu as it was (its music kept playing)
        going_back = false;
        g_setup = nullptr;
        setup.reset();
        g_ui = ui.get();
      } else if (leaving && (black += dt / 0.3f) >= 1.0f) {
        // Faded to black: the chosen game replaces any earlier one (the menu
        // reading it goes first), then the menu and its music fade in.
        leaving = false;
        g_setup = nullptr;
        setup.reset();
        g_ui = nullptr;
        g_audio = nullptr;
        audio.reset();
        ui.reset();
        files.reset();
        source = std::move(chosen);
        SaveGameLocation(data_folder, source->location());
        launcher::Log("game chosen: %s", source->location().u8string().c_str());
        if (!load_menu()) return 2;
        last = std::chrono::steady_clock::now();
      }
    } else if (black > 0) {
      black -= dt / 0.6f;
    }
    if (ms(t_start, t_end) > 200) {
      launcher::Log("slow frame %.0f ms: messages %.0f, controller %.0f, audio %.0f, ui %.0f, "
                    "draw %.0f, present %.0f%s",
                    ms(t_start, t_end), ms(t_start, t_messages), ms(t_messages, t_controller),
                    ms(t_controller, t_audio), ms(t_audio, t_ui), ms(t_ui, t_draw),
                    ms(t_draw, t_end), GetForegroundWindow() == g_window ? "" : " (in background)");
    }
  }

  if (!(GetWindowLongW(g_window, GWL_STYLE) & WS_CAPTION)) ToggleFullscreen();
  SaveWindowPlacement(data_folder);
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  return 0;
}
