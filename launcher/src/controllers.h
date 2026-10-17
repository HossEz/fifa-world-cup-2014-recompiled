// Controllers through SDL3 (Xbox, PlayStation, Switch and others), on a
// thread of their own.
//
// SDL's set-up on Windows ties itself to the thread that initialises it: done
// on the window's thread, Windows declared the launcher "not responding" a few
// seconds after it lost focus, while its loop ran normally (IsHungAppWindow,
// checked 2026-10-16). Here SDL lives on its own thread, which initialises it,
// pumps its events and polls the controllers; the window's thread only reads
// the merged button state.

#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace launcher {

class Controllers {
 public:
  // Buttons merged over all connected controllers (left stick as d-pad).
  enum : uint32_t {
    kUp = 1, kDown = 2, kLeft = 4, kRight = 8,
    kSouth = 16,  // A / Cross
    kEast = 32,   // B / Circle
    kNorth = 64,  // Y / Triangle
  };

  ~Controllers();
  void Start();
  uint32_t Buttons() const { return buttons_.load(std::memory_order_relaxed); }
  // The last connected controller is a PlayStation one (for the hints).
  bool PlayStation() const { return playstation_.load(std::memory_order_relaxed); }

 private:
  void Run();
  std::thread thread_;
  std::atomic<bool> quit_{false};
  std::atomic<uint32_t> buttons_{0};
  std::atomic<bool> playstation_{false};
};

}  // namespace launcher
