// Bigger graphics memory pool, for stadium mods larger than the console allows.
//
// The game's memory framework sets up its pools at boot from memoryfw.ini
// (d:\memoryfw.ini, or a RefPack-compressed copy in the image at 0x83BB6238),
// parsed by sub_83670158(text, length, ...). Textures and models (category
// XRendering, also XTL and XMovie) come from XPhys4KPP, a fixed 227104k of
// physical memory. A modded stadium that doesn't fit makes the framework stop
// the game (sub_8366E140: "Out of memory ... from category ...", then
// DebugBreak; issue #21, stadiums over ~55 MB).
//
// The console spends physical memory on its OS and on the code image; the PC
// build doesn't, so XPhys4KPP gets what is free at boot, less the heaps outside
// the container and a reserve. Measured with the pool at 170 MB: 492 MB free
// before the pools, 290 after (the container isn't physical memory), 288 at a
// match's load. A 169 MB modded stadium ran the pool out at a match's training
// drill while 61 of a 64 MB reserve were still free, so the reserve is 16 MB.

#include <cstring>
#include <string_view>

#include <fmt/format.h>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

REX_EXTERN(__imp__sub_83670158);
REX_EXTERN(__imp__sub_8366E140);

namespace {
constexpr std::string_view kPhysPoolLine = "XPhys4KPP\t\tXPhysical4kAllocator\t[ size=";
constexpr uint32_t kOriginalKb = 227104;
// The Xbox 360 heaps outside the container (SRenderingPP, RenderingPP, GTRenderingPP, ShaderPP, RenderingRain).
constexpr uint32_t kOtherPoolsKb = 32576;
constexpr uint32_t kReserveKb = 16 * 1024;

uint32_t FreePhysicalKb() {
  auto* heap = REX_KERNEL_MEMORY()->GetPhysicalHeap();
  return static_cast<uint32_t>(uint64_t(heap->GetUnreservedPageCount()) * heap->page_size() / 1024);
}
}  // namespace

REX_HOOK_RAW(sub_83670158) {
  char* text = REX_KERNEL_MEMORY()->TranslateVirtual<char*>(ctx.r3.u32);
  std::string_view script(text, ctx.r4.u32);
  size_t at = script.find(kPhysPoolLine);
  // The size is written back in place with the same six digits, so the length stays.
  if (at != std::string_view::npos &&
      script.substr(at + kPhysPoolLine.size(), 7) == fmt::format("{}k", kOriginalKb)) {
    uint32_t free_kb = FreePhysicalKb();
    uint32_t size_kb = kOriginalKb;
    if (free_kb > kOtherPoolsKb + kReserveKb + kOriginalKb) {
      size_kb = (free_kb - kOtherPoolsKb - kReserveKb) & ~63u;  // the script keeps 64k steps
    }
    std::string digits = fmt::format("{:06}", size_kb);
    std::memcpy(text + at + kPhysPoolLine.size(), digits.data(), 6);
    REXLOG_INFO("memory pools: graphics pool {} MB (console {} MB), {} MB physical free before",
                size_kb / 1024, kOriginalKb / 1024, free_kb / 1024);
    __imp__sub_83670158(ctx, base);
    REXLOG_INFO("memory pools: {} MB physical free after the pools", FreePhysicalKb() / 1024);
    return;
  }
  __imp__sub_83670158(ctx, base);
}

// The framework's out-of-memory stop: r3 points at {category, name, size}.
REX_HOOK_RAW(sub_8366E140) {
  auto* info = REX_KERNEL_MEMORY()->TranslateVirtual<rex::be<uint32_t>*>(ctx.r3.u32);
  auto str = [](uint32_t address) {
    return address ? REX_KERNEL_MEMORY()->TranslateVirtual<const char*>(address) : "?";
  };
  REXLOG_ERROR("game out of memory: {} bytes for '{}' from category '{}', {} MB physical free",
               uint32_t(info[2]), str(info[1]), str(info[0]), FreePhysicalKb() / 1024);
  __imp__sub_8366E140(ctx, base);
}
