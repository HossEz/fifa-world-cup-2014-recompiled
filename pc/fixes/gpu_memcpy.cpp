// Resolve-aware memcpy for default.xex.
//
// FIFA renders kit composites, shadow/stadium maps and mip chains on the GPU,
// resolves them to memory, then copies them on the CPU with the CRT memcpy
// (sub_82431150) into the buffers the GPU later samples. On the console the GPU
// writes guest memory directly; here the newest resolve output lives only in GPU
// memory unless it is read back, which stalls.
//
// When the source holds GPU-newer data and the destination is GPU-visible
// physical memory, the copy is done inside GPU memory instead, ordered before
// the guest's next GPU commands. Everything else takes the original memcpy.

#include <atomic>
#include <chrono>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

REX_EXTERN(__imp__sub_82431150);

namespace {

// Physical memory views: 0xA0000000 (64K pages), 0xC0000000 (16M), 0xE0000000 (4K).
constexpr uint32_t kPhysicalViewsBase = 0xA0000000;

std::atomic<uint64_t> g_gpu_copies{0};
std::atomic<uint64_t> g_gpu_bytes{0};
std::atomic<uint64_t> g_stale_cpu_copies{0};  // GPU-newer source, CPU-only destination
std::atomic<int64_t> g_last_report_ms{0};

void MaybeReport() {
  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
  int64_t last = g_last_report_ms.load(std::memory_order_relaxed);
  if (now - last < 5000 ||
      !g_last_report_ms.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
    return;
  }
  REXLOG_INFO("gpu_memcpy: {} GPU-side copies ({} KB), {} stale CPU copies so far",
              g_gpu_copies.load(), g_gpu_bytes.load() >> 10, g_stale_cpu_copies.load());
}

}  // namespace

REX_HOOK_RAW(sub_82431150) {
  uint32_t dst = ctx.r3.u32;
  uint32_t src = ctx.r4.u32;
  uint32_t length = ctx.r5.u32;
  if (length && src >= kPhysicalViewsBase) {
    auto* graphics = rex::Runtime::instance()->graphics_system();
    auto* memory = REX_KERNEL_MEMORY();
    uint32_t src_physical = memory->GetPhysicalAddress(src);
    if (graphics && src_physical != UINT32_MAX &&
        graphics->IsGpuMemoryNewer(src_physical, length)) {
      uint32_t dst_physical =
          dst >= kPhysicalViewsBase ? memory->GetPhysicalAddress(dst) : UINT32_MAX;
      if (dst_physical != UINT32_MAX &&
          graphics->CopyGpuMemory(dst_physical, src_physical, length)) {
        g_gpu_copies.fetch_add(1, std::memory_order_relaxed);
        g_gpu_bytes.fetch_add(length, std::memory_order_relaxed);
        MaybeReport();
        return;  // memcpy returns dst, still in r3
      }
      g_stale_cpu_copies.fetch_add(1, std::memory_order_relaxed);
      MaybeReport();
    }
  }
  __imp__sub_82431150(ctx, base);
}

// XGUntileSurface (XDK): converts a tiled surface, typically a resolved render
// target, to linear layout on the CPU in 16-byte memcpys (see the caller of
// sub_82431150 at 0x824300D4). Taking those through the GPU-copy path above would
// mean millions of tiny copies, so instead read the whole tiled source back once.
//   r3 dst, r4 row pitch, r5 POINT* dst offset, r6 src, r7 width, r8 height,
//   r9 RECT* src rect (null = whole surface), r10 texel pitch (bytes)
REX_EXTERN(__imp__sub_8242FE00);

namespace {
std::atomic<uint64_t> g_untile_readbacks{0};
}  // namespace

// The game untiles a kit texture in strips of 32 rows (1024x32, 128 KB each),
// and every readback waits for the GPU to finish all queued work: ~30 a second
// in kit select and the training intro, the game thread waiting for over half
// of the time at 4K (measured 2026-10-24). The strips of one surface are
// contiguous (tiled 2D surfaces are stored in bands of 32 rows), so the first
// strip reads back the whole run of GPU-newer pages from there, and the
// following strips find their data already current: ~15x less waiting.
template <typename Graphics>
uint32_t UntileReadbackLength(Graphics* graphics, uint32_t start, uint32_t length) {
  constexpr uint32_t kPage = 0x1000;
  constexpr uint32_t kMaxRun = 0x1000000;
  uint32_t end = (start + length + kPage - 1) & ~(kPage - 1);
  while (end - start < kMaxRun && graphics->IsGpuMemoryNewer(end, kPage)) {
    end += kPage;
  }
  return end - start;
}

REX_HOOK_RAW(sub_8242FE00) {
  uint32_t src = ctx.r6.u32;
  if (src >= kPhysicalViewsBase) {
    uint64_t width = (ctx.r7.u32 + 31) & ~31u;
    uint64_t height = (ctx.r8.u32 + 31) & ~31u;
    uint64_t length = width * height * ctx.r10.u32;
    auto* graphics = rex::Runtime::instance()->graphics_system();
    uint32_t src_physical = REX_KERNEL_MEMORY()->GetPhysicalAddress(src);
    if (graphics && src_physical != UINT32_MAX && length && length <= 0x4000000 &&
        graphics->IsGpuMemoryNewer(src_physical, uint32_t(length)) &&
        graphics->ReadbackGpuMemory(
            src_physical, UntileReadbackLength(graphics, src_physical, uint32_t(length)))) {
      uint64_t count = g_untile_readbacks.fetch_add(1, std::memory_order_relaxed) + 1;
      if (count <= 20 || count % 500 == 0) {
        // First and middle words of the read-back source (comparing backends).
        auto* words = REX_KERNEL_MEMORY()->TranslatePhysical<rex::be<uint32_t>*>(src_physical);
        REXLOG_INFO("gpu_memcpy: untile readback #{} src {:08X} {}x{} x{} B, first {:08X} "
                    "middle {:08X}",
                    count, src, ctx.r7.u32, ctx.r8.u32, ctx.r10.u32, uint32_t(words[0]),
                    uint32_t(words[length / 8]));
      }
    }
  }
  __imp__sub_8242FE00(ctx, base);
}

// sub_82FDD188 samples one pixel of a GPU-rendered, tiled RGBA8 surface (crowd
// colours: black fans without this), converts it to RGB565 and fills a strip.
// Called once per pixel index, so read the surface back on the first call:
//   r4 -> { u32 surface base, u32 row pitch (bytes), u32 pixel index }
// The height is not passed; read back the run of GPU-newer pages from the base.
REX_EXTERN(__imp__sub_82FDD188);

namespace {

constexpr uint32_t kPageSize = 0x1000;
constexpr uint32_t kMaxSurfaceReadback = 0x400000;

// Reads back the contiguous GPU-newer pages starting at a guest address.
bool ReadbackGpuNewerRun(uint32_t guest_address) {
  if (guest_address < kPhysicalViewsBase) {
    return false;
  }
  auto* graphics = rex::Runtime::instance()->graphics_system();
  uint32_t physical = REX_KERNEL_MEMORY()->GetPhysicalAddress(guest_address);
  if (!graphics || physical == UINT32_MAX || !graphics->IsGpuMemoryNewer(physical, 1)) {
    return false;
  }
  uint32_t start = physical & ~(kPageSize - 1);
  uint32_t end = start + kPageSize;
  while (end - start < kMaxSurfaceReadback && graphics->IsGpuMemoryNewer(end, kPageSize)) {
    end += kPageSize;
  }
  return graphics->ReadbackGpuMemory(start, end - start);
}

std::atomic<uint64_t> g_crowd_readbacks{0};

}  // namespace

REX_HOOK_RAW(sub_82FDD188) {
  uint32_t surface = REX_KERNEL_MEMORY()->TranslateVirtual<rex::be<uint32_t>*>(ctx.r4.u32)[0];
  if (ReadbackGpuNewerRun(surface)) {
    uint64_t count = g_crowd_readbacks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 10 || count % 100 == 0) {
      REXLOG_INFO("gpu_memcpy: tiled-sample readback #{} surface {:08X}", count, surface);
    }
  }
  __imp__sub_82FDD188(ctx, base);
}
