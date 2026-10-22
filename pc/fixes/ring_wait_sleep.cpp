// The game's renderer waits for room in the GPU command ring in a loop
// (sub_8244D1F8): each round, sub_82451D98 spins 32 db16cyc hints, checks
// whether to stop, and returns 1 to keep waiting. On the Xbox 360 the spin cost
// nothing; on a PC it kept a core at 100% (a laptop's CPU ran hot at 40% total
// use). The hook sleeps 0.1 ms whenever the wait goes on: the renderer still
// sees room within a fraction of a frame, and the core rests meanwhile.

#include <chrono>

#include <rex/hook.h>
#include <rex/thread.h>

REX_EXTERN(__imp__sub_82451D98);

REX_HOOK_RAW(sub_82451D98) {
  __imp__sub_82451D98(ctx, base);
  if (ctx.r3.u32 == 1) {
    rex::thread::Sleep(std::chrono::microseconds(100));
  }
}
