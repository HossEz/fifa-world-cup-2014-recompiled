// The present interval: how many vblanks each frame stays on screen.
//
// sub_82453028 (D3D, statically linked into default.xex) queues a frame for
// display. Its argument packs the front buffer address (bits 12-31), the
// present interval (bits 8-11) and a lateness tolerance (bits 0-7). The frame is
// scheduled for vblank `max(last frame's vblank + interval, current vblank)`, and
// the vblank interrupt handler (sub_82452EF8) flips to it once that vblank
// arrives. The game asks for interval 2 in cutscenes and many menus (kick-off
// controller select, team and kit select): every frame stays up for two
// refreshes, 30 FPS at 60 Hz, however fast the frame was made.
//
// present_interval_cap lowers larger intervals to the cap: 1 lets those scenes
// run at the refresh rate. Interval 0 (no wait) is left alone. Requested
// intervals are counted and logged every 10 s, and every frame is counted for
// the PC version's frame-rate display (present_interval.h).
//
// The tolerance is how far into a vblank period, in percent, a late frame may
// still flip right away. Past it, the frame waits for the next vblank and the
// game waits with it, so frames just over the period run at half the rate: 72
// FPS at 144 Hz. The game passes 0, so every late frame waited.
// present_late_tolerance replaces it; 100 flips every late frame at once. Measured
// at 60 FPS and 4K: frames over 1.5x the median 291 -> 35 per 2.5 min with FreeSync
// and 102 -> 81 at 60 Hz without it, at the same average frame rate or better.
//
// Measured with the game's own frame limiter (sub_82366AB0) for comparison: it is
// called with 200 and 40 FPS targets only, so it isn't what holds menus at 30.

#include "present_interval.h"

#include <array>
#include <atomic>
#include <chrono>
#include <string>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(present_interval_cap, 0, "Game",
                     "Highest present interval (vblanks per frame) the game may use. It asks "
                     "for 2 in cutscenes and many menus (30 FPS at 60 Hz); 1 shows every frame. "
                     "0 keeps the game's interval.")
    .range(0, 15);

REXCVAR_DEFINE_INT32(present_late_tolerance, 100, "Game",
                     "How far into a vblank period, in percent, a late frame may still flip right "
                     "away instead of waiting for the next vblank. 0 keeps the game's value.")
    .range(0, 100);

REX_EXTERN(__imp__sub_82453028);

namespace {

std::array<std::atomic<uint32_t>, 16> g_interval_counts{};
std::atomic<uint64_t> g_frames_queued{0};
std::atomic<uint32_t> g_capped{0};
std::atomic<uint32_t> g_game_tolerance{0};
std::atomic<int64_t> g_last_report_ms{0};

void MaybeReport() {
  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
  int64_t last = g_last_report_ms.load(std::memory_order_relaxed);
  if (now - last < 10000 ||
      !g_last_report_ms.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
    return;
  }
  std::string text;
  for (uint32_t i = 0; i < g_interval_counts.size(); ++i) {
    uint32_t count = g_interval_counts[i].exchange(0, std::memory_order_relaxed);
    if (count) {
      text += fmt::format(" interval {}: {}", i, count);
    }
  }
  uint32_t capped = g_capped.exchange(0, std::memory_order_relaxed);
  if (!text.empty()) {
    REXLOG_INFO("present intervals in the last 10 s:{} ({} capped to {}), game's late tolerance {}",
                text, capped, REXCVAR_GET(present_interval_cap),
                g_game_tolerance.load(std::memory_order_relaxed));
  }
}

}  // namespace

uint64_t GuestFramesQueued() { return g_frames_queued.load(std::memory_order_relaxed); }

REX_HOOK_RAW(sub_82453028) {
  g_frames_queued.fetch_add(1, std::memory_order_relaxed);
  uint32_t interval = (ctx.r3.u32 >> 8) & 0xF;
  g_interval_counts[interval].fetch_add(1, std::memory_order_relaxed);
  uint32_t cap = uint32_t(REXCVAR_GET(present_interval_cap));
  if (cap && interval > cap) {
    ctx.r3.u64 = (ctx.r3.u32 & ~uint32_t(0xF00)) | (cap << 8);
    g_capped.fetch_add(1, std::memory_order_relaxed);
  }
  g_game_tolerance.store(ctx.r3.u32 & 0xFF, std::memory_order_relaxed);
  uint32_t tolerance = uint32_t(REXCVAR_GET(present_late_tolerance));
  if (tolerance) {
    ctx.r3.u64 = (ctx.r3.u32 & ~uint32_t(0xFF)) | tolerance;
  }
  MaybeReport();
  __imp__sub_82453028(ctx, base);
}
