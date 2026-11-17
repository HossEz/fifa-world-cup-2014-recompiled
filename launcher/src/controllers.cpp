#include "controllers.h"

#include <chrono>
#include <cmath>
#include <vector>

#include <SDL3/SDL.h>
#include <windows.h>

#include "log.h"

namespace launcher {
namespace {

// A Sony game controller among the HID devices, also one hidden behind an
// emulated Xbox pad.
bool SonyControllerConnected() {
  UINT count = 0;
  if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || !count) return false;
  std::vector<RAWINPUTDEVICELIST> devices(count);
  if (GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) return false;
  for (const RAWINPUTDEVICELIST& device : devices) {
    if (device.dwType != RIM_TYPEHID) continue;
    RID_DEVICE_INFO info{};
    info.cbSize = sizeof(info);
    UINT size = sizeof(info);
    if (GetRawInputDeviceInfoW(device.hDevice, RIDI_DEVICEINFO, &info, &size) == UINT(-1)) continue;
    if (info.hid.dwVendorId == 0x054C && info.hid.usUsagePage == 1 &&
        (info.hid.usUsage == 4 || info.hid.usUsage == 5)) {
      return true;
    }
  }
  return false;
}

}  // namespace

Controllers::~Controllers() {
  quit_ = true;
  if (thread_.joinable()) thread_.join();
}

void Controllers::Start() { thread_ = std::thread([this] { Run(); }); }

void Controllers::Run() {
  if (!SDL_Init(SDL_INIT_GAMEPAD)) {
    Log("controllers: SDL failed: %s", SDL_GetError());
    return;
  }
  std::vector<SDL_Gamepad*> pads;
  int known = -1;
  const Sint16 dead = 16000;
  while (!quit_) {
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);  // only the state below is used
    SDL_UpdateGamepads();
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (count != known) {
      for (SDL_Gamepad* pad : pads) SDL_CloseGamepad(pad);
      pads.clear();
      for (int i = 0; i < count; ++i) {
        if (SDL_Gamepad* pad = SDL_OpenGamepad(ids[i])) {
          pads.push_back(pad);
          Log("controller: %s", SDL_GetGamepadName(pad));
          SDL_GamepadType type = SDL_GetGamepadType(pad);
          playstation_ = type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 ||
                         type == SDL_GAMEPAD_TYPE_PS5;
        }
      }
      // A PlayStation pad DS4Windows and the like show as an Xbox 360 one still
      // has PlayStation buttons; the game's Automatic button icons test the same
      // (pc/src/game_files.cpp).
      if (count > 0 && SonyControllerConnected()) playstation_ = true;
      if (count == 0 && known > 0) Log("controller: none connected");
      known = count;
    }
    SDL_free(ids);
    uint32_t buttons = 0;
    float scroll = 0.0f;
    for (SDL_Gamepad* pad : pads) {
      if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) buttons |= kUp;
      if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) buttons |= kDown;
      if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) buttons |= kLeft;
      if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) buttons |= kRight;
      if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)) buttons |= kSouth;
      if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST)) buttons |= kEast;
      if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH)) buttons |= kNorth;
      Sint16 x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
      Sint16 y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
      if (y < -dead) buttons |= kUp;
      if (y > dead) buttons |= kDown;
      if (x < -dead) buttons |= kLeft;
      if (x > dead) buttons |= kRight;
      // The right stick scrolls, from a smaller dead zone than the menus' moves.
      float ry = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
      if (std::abs(ry) > 0.25f && std::abs(ry) > std::abs(scroll)) scroll = ry;
    }
    buttons_ = buttons;
    scroll_ = scroll;
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }
  for (SDL_Gamepad* pad : pads) SDL_CloseGamepad(pad);
  SDL_Quit();
}

}  // namespace launcher
