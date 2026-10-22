// The game's frame limiter.
//
// sub_82366AB0(limiter, fps) spins on the time base until `last frame start +
// ticks per second / fps`, then starts the next frame. It is called with 200 (no
// real limit) in play and menus, and 40 in cutscenes: the pre-match intro
// (line-ups, fans, stadium) and goal celebrations run at exactly 40 FPS.
// frame_rate_floor raises a lower target to the floor, for example 40 -> 60.
// Changes of the target are logged ("frame limiter target").
//
// The spin kept a core at 100% for much of every frame (on the Xbox 360 it cost
// nothing; on a laptop the CPU ran hot at 40% total use), so the hook sleeps
// until just before the deadline and leaves the last 0.5 ms to the game's loop:
// frames start at the same moment as before.
//
// The other half of the 30/40 FPS limits is the present interval
// (pc/fixes/present_interval.cpp).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/thread.h>

REXCVAR_DEFINE_INT32(frame_rate_floor, 0, "Game",
                     "Raise the game's own frame-rate target to at least this many FPS (it "
                     "asks for 40 in cutscenes). 0 keeps the game's target.")
    .range(0, 240);

REX_EXTERN(__imp__sub_82366AB0);

namespace {
std::atomic<int32_t> g_last_logged_target{-1};
std::atomic<uint32_t> g_logged_changes{0};

uint64_t LoadGuestU64(uint8_t* base, uint32_t address) {
  return __builtin_bswap64(*reinterpret_cast<volatile uint64_t*>(base + address));
}

// Sleeps until 0.5 ms before the limiter's deadline: the u64 at limiter + 24
// (time base ticks per second) divided by the fps, plus the u64 at limiter + 0
// (the last frame's start), as sub_82366AB0 computes it.
void SleepUntilNearDeadline(uint8_t* base, uint32_t limiter, int32_t fps) {
  if (fps <= 0) {
    return;
  }
  int64_t deadline =
      int64_t(LoadGuestU64(base, limiter + 24)) / fps + int64_t(LoadGuestU64(base, limiter));
  int64_t frequency = int64_t(rex::chrono::Clock::guest_tick_frequency());
  int64_t margin = frequency / 2000;
  while (true) {
    int64_t remaining = deadline - int64_t(rex::chrono::Clock::QueryGuestTickCount()) - margin;
    if (remaining <= 0) {
      return;
    }
    // At most 2 ms at a time: longer sleeps fall back to whole-millisecond ones,
    // which can overshoot by a timer tick.
    rex::thread::Sleep(
        std::chrono::microseconds(std::min<int64_t>(remaining * 1000000 / frequency, 2000)));
  }
}
}  // namespace

REX_HOOK_RAW(sub_82366AB0) {
  int32_t target = ctx.r4.s32;
  // Two limiters alternate (200 and 40) while a cutscene runs: log only targets
  // other than 200, and only when they change, to keep the log short.
  if (target != 200 && g_last_logged_target.exchange(target, std::memory_order_relaxed) != target &&
      g_logged_changes.fetch_add(1, std::memory_order_relaxed) < 100) {
    REXLOG_INFO("frame limiter target {} fps", target);
  }
  int32_t floor = REXCVAR_GET(frame_rate_floor);
  if (floor > 0 && target > 0 && target < floor) {
    ctx.r4.s64 = floor;
  }
  SleepUntilNearDeadline(base, ctx.r3.u32, ctx.r4.s32);
  __imp__sub_82366AB0(ctx, base);
}
