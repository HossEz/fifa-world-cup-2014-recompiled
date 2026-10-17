// Fixes for start-up races in default.xex that the console's thread timing
// never exposes.
//
// sub_836CE548 deletes an object: it calls the object's destructor, then frees
// it through the global allocator at 0x83D568F4 (vtable slot 3). During boot a
// thread loading the UI can delete an object before another thread has set
// that allocator up; the original then calls through a null vtable and the game
// stops just before PRESS START (seen intermittently on slower machines).
// While the allocator isn't set, the object is left alone instead (a one-time
// leak of a few bytes). The same goes for an object whose vtable has no
// destructor: it isn't a live object (a loading thread got to it first).
//
// sub_8235C320 starts the game's systems. It creates a short-lived thread
// (entry sub_82359B58) that sets up the memory heaps, among them the heap
// table at 0x83C9BF88 (sub_8247E668), then goes on to sub_8235BD88, which
// allocates from heap 0 (sub_826EB408 -> allocator sub_8247E6F0). On the
// console the new thread has finished long before. On a slow PC (a dual-core
// laptop under load) it may not even have started: the table slot is still
// 0 and the allocator calls through a null vtable (crash ~45 s into boot,
// every time on such a laptop). sub_8235BD88 now waits for the heap thread to
// finish first, as the console's timing guarantees. The heap thread takes no
// lock the starting thread holds, so the wait can't deadlock; it is capped
// anyway.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

REX_EXTERN(__imp__sub_836CE548);
REX_EXTERN(__imp__sub_82359B58);
REX_EXTERN(__imp__sub_8235BD88);

namespace {
constexpr uint32_t kGlobalAllocator = 0x83D568F4;
std::atomic<uint32_t> g_skipped_deletes{0};

constexpr auto kHeapSetupWaitLimit = std::chrono::seconds(30);
std::mutex g_heap_setup_mutex;
std::condition_variable g_heap_setup_done_cv;
bool g_heap_setup_done = false;
}  // namespace

// The heap set-up thread's entry: run it, then let waiters go.
REX_HOOK_RAW(sub_82359B58) {
  __imp__sub_82359B58(ctx, base);
  {
    std::lock_guard lock(g_heap_setup_mutex);
    g_heap_setup_done = true;
  }
  g_heap_setup_done_cv.notify_all();
}

// The first allocation after starting that thread.
REX_HOOK_RAW(sub_8235BD88) {
  {
    std::unique_lock lock(g_heap_setup_mutex);
    if (!g_heap_setup_done) {
      auto started = std::chrono::steady_clock::now();
      bool done = g_heap_setup_done_cv.wait_for(lock, kHeapSetupWaitLimit,
                                                [] { return g_heap_setup_done; });
      auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count();
      if (done) {
        REXLOG_INFO("boot race: waited {} ms for the heap set-up thread", waited);
      } else {
        REXLOG_WARN("boot race: heap set-up thread still not done after {} ms, going on",
                    waited);
      }
    }
  }
  __imp__sub_8235BD88(ctx, base);
}

REX_HOOK_RAW(sub_836CE548) {
  if (ctx.r3.u32) {
    auto* memory = REX_KERNEL_MEMORY();
    auto load = [memory](uint32_t address) {
      return uint32_t(*memory->TranslateVirtual<rex::be<uint32_t>*>(address));
    };
    uint32_t allocator = load(kGlobalAllocator);
    if (!allocator) {
      if (g_skipped_deletes.fetch_add(1) < 10) {
        REXLOG_WARN("boot race: delete of {:08X} before the global allocator exists, skipped",
                    ctx.r3.u32);
      }
      return;
    }
    // An object without a destructor in its vtable isn't a live object (not
    // yet constructed, or already destroyed by another loading thread): seen
    // on a slow laptop ~110 s into boot, from a resource's destructor
    // (sub_82387810) deleting its member. Valid objects never have slot 0
    // empty, so leave it alone instead of calling address 0.
    uint32_t vtable = load(ctx.r3.u32);
    if (!vtable || !load(vtable)) {
      if (g_skipped_deletes.fetch_add(1) < 10) {
        REXLOG_WARN("boot race: delete of {:08X} (vtable {:08X}) with no destructor, skipped; "
                    "called from {:08X}",
                    ctx.r3.u32, vtable, uint32_t(ctx.lr));
      }
      return;
    }
  }
  __imp__sub_836CE548(ctx, base);
}
