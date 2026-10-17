#include "controllers.h"

#include <chrono>
#include <vector>

#include <SDL3/SDL.h>

#include "log.h"

namespace launcher {

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
      if (count == 0 && known > 0) Log("controller: none connected");
      known = count;
    }
    SDL_free(ids);
    uint32_t buttons = 0;
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
    }
    buttons_ = buttons;
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }
  for (SDL_Gamepad* pad : pads) SDL_CloseGamepad(pad);
  SDL_Quit();
}

}  // namespace launcher
