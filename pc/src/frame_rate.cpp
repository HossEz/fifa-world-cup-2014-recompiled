// The game's frame limiter.
//
// sub_82366AB0(limiter, fps) spins on the time base until `last frame start +
// ticks per second / fps`, then starts the next frame. It is called with 200 (no
// real limit) in play and menus, and 40 in cutscenes: the pre-match intro
// (line-ups, fans, stadium) and goal celebrations run at exactly 40 FPS.
// frame_rate_floor raises a lower target to the floor, for example 40 -> 60.
// Changes of the target are logged ("frame limiter target").
//
// The other half of the 30/40 FPS limits is the present interval
// (pc/fixes/present_interval.cpp).

#include <atomic>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(frame_rate_floor, 0, "Game",
                     "Raise the game's own frame-rate target to at least this many FPS (it "
                     "asks for 40 in cutscenes). 0 keeps the game's target.")
    .range(0, 240);

REX_EXTERN(__imp__sub_82366AB0);

namespace {
std::atomic<int32_t> g_last_logged_target{-1};
std::atomic<uint32_t> g_logged_changes{0};
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
  __imp__sub_82366AB0(ctx, base);
}
