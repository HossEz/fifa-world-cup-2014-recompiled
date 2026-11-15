// Everything from the EA Sports Football Club catalogue is unlocked.
//
// The catalogue (classic kits, celebrations and the rest) needed EA's servers,
// which are gone, so its items can't be bought any more (issue #20). The game
// has settings that unlock them, read from locale.ini through
// sub_823D2A08(settings, key, default, ...), which returns the value:
//   POW/ENABLE_ALL_UNLOCKABLES  catalogue items (powdllzf sub_89739F00, which
//                               calls this through the main game's function table)
//   UNLOCK_ALL_UCC              all celebrations (sub_8307EDE8)
// Both now read as 1, as if set in locale.ini.

#include <atomic>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

REX_EXTERN(__imp__sub_823D2A08);

REX_HOOK_RAW(sub_823D2A08) {
  static std::atomic<bool> logged[2];
  const char* key = REX_KERNEL_MEMORY()->TranslateVirtual<const char*>(ctx.r4.u32);
  int which = !ctx.r4.u32                                          ? -1
              : std::strcmp(key, "POW/ENABLE_ALL_UNLOCKABLES") == 0 ? 0
              : std::strcmp(key, "UNLOCK_ALL_UCC") == 0             ? 1
                                                                    : -1;
  if (which >= 0) {
    if (!logged[which].exchange(true)) REXLOG_INFO("catalogue unlocks: {} = 1", key);
    ctx.r3.u64 = 1;
    return;
  }
  __imp__sub_823D2A08(ctx, base);
}
