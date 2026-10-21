// Lua errors caught by the game's scripts (default.xex embeds Lua 5.1).
//
// sub_838A9740 is luaD_rawrunprotected: it links a lua_longjmp into
// L->errorJmp, calls setjmp (sub_839E5AC0) and runs a function. On an error,
// sub_838AA118 (luaD_throw) longjmps (sub_839E72B0) back to it. Recompiled, the
// guest longjmp restores the guest registers but then returns to luaD_throw
// instead of to the setjmp: luaD_throw goes on as if nothing caught the error,
// to the panic and exit(), and the game closes (KeBugCheck, 0x80000003). Seen
// in the World Cup training drills (heading) on a script error the game
// itself catches.
//
// Both are done here with the host's setjmp/longjmp: the protected call keeps
// the guest frame and lua_longjmp of the original (only the guest jmp_buf goes
// unused), and the throw jumps to the host jmp_buf of the protected call that
// L->errorJmp names, with the guest registers as they were when it started.

#include <csetjmp>
#include <vector>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

REX_EXTERN(__imp__sub_838AA118);

namespace {
// lua_State and the protected call's frame, from sub_838A9740 / sub_838AA118.
constexpr uint32_t kErrorJmp = 112;     // lua_State: the innermost lua_longjmp
constexpr uint32_t kFrameSize = 1472;   // the original's guest frame
constexpr uint32_t kLongJmp = 80;       // its lua_longjmp, in that frame
constexpr uint32_t kPrevious = 0;       // lua_longjmp: the enclosing one
constexpr uint32_t kStatus = 1360;      // lua_longjmp: the error code

struct ProtectedCall {
  uint32_t long_jmp;  // its lua_longjmp (guest)
  std::jmp_buf* host;
};
// The protected calls running on this thread, innermost last.
thread_local std::vector<ProtectedCall> g_protected_calls;

uint32_t g_caught_errors = 0;

uint32_t Load(uint32_t address) {
  return *REX_KERNEL_MEMORY()->TranslateVirtual<rex::be<uint32_t>*>(address);
}
void Store(uint32_t address, uint32_t value) {
  *REX_KERNEL_MEMORY()->TranslateVirtual<rex::be<uint32_t>*>(address) = value;
}
}  // namespace

// luaD_rawrunprotected(L, f, ud): runs f(L, ud), returns the error code (0 if none).
REX_HOOK_RAW(sub_838A9740) {
  const uint32_t L = ctx.r3.u32;
  const uint32_t f = ctx.r4.u32;
  const uint32_t ud = ctx.r5.u32;
  const uint32_t sp = ctx.r1.u32;
  const uint32_t frame = sp - kFrameSize;
  const uint32_t long_jmp = frame + kLongJmp;
  Store(frame, sp);  // back chain
  Store(long_jmp + kPrevious, Load(L + kErrorJmp));
  Store(long_jmp + kStatus, 0);
  Store(L + kErrorJmp, long_jmp);

  const PPCContext entry = ctx;
  std::jmp_buf host;
  const size_t depth = g_protected_calls.size();
  g_protected_calls.push_back({long_jmp, &host});
  if (setjmp(host) == 0) {
    ctx.r1.u32 = frame;
    ctx.r3.u64 = L;
    ctx.r4.u64 = ud;
    rex::runtime::ResolveIndirectFunction(f)(ctx, base);
  }
  // After a throw, the calls it jumped out of are gone too.
  g_protected_calls.resize(depth);

  uint32_t status = Load(long_jmp + kStatus);
  if (status) {
    ctx = entry;  // the registers as the setjmp would have restored them
    if (g_caught_errors++ < 10) {
      REXLOG_INFO("lua: an error caught by the script (status {})", status);
    }
  }
  Store(L + kErrorJmp, Load(long_jmp + kPrevious));
  ctx.r1.u32 = sp;
  ctx.lr = entry.lr;
  ctx.r3.u64 = status;
}

// luaD_throw(L, errcode): to the protected call in L->errorJmp, or the panic.
REX_HOOK_RAW(sub_838AA118) {
  uint32_t long_jmp = Load(ctx.r3.u32 + kErrorJmp);
  if (long_jmp) {
    for (auto call = g_protected_calls.rbegin(); call != g_protected_calls.rend(); ++call) {
      if (call->long_jmp == long_jmp) {
        Store(long_jmp + kStatus, ctx.r4.u32);
        std::longjmp(*call->host, 1);
      }
    }
    REXLOG_WARN("lua: error {} with no protected call of this thread at {:08X}", ctx.r4.u32,
                long_jmp);
  }
  __imp__sub_838AA118(ctx, base);
}
