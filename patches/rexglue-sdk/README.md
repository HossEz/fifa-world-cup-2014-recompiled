# rexglue-sdk patches

Base: [rexglue-sdk](https://github.com/rexglue/rexglue-sdk) `development` @ `bd833a2`.

The patches have LF line endings and only apply to an SDK checkout with LF
line endings. Git for Windows converts to CRLF by default (`core.autocrlf`),
so clone the SDK without that conversion:

```
git clone -c core.autocrlf=false https://github.com/rexglue/rexglue-sdk
cd rexglue-sdk
git checkout bd833a2
git submodule update --init --recursive
```

Apply them all in one go from the SDK checkout (they apply in order on a clean
`bd833a2`), then build and install the SDK:

```
git apply <this repo>/patches/rexglue-sdk/*.patch
cmake --preset win-amd64
cmake --build out/build/win-amd64 --config RelWithDebInfo --target install
cmake --build out/build/win-amd64 --config Release --target install
```

Install both configurations: the game's build runs code generation with the
installed Release `rexglue.exe`, and links the RelWithDebInfo runtime.

The GPU plugin and runtime DLLs are only copied next to the game when the exe
relinks, so after an SDK-only rebuild copy `rexgpu-xenos*.dll` and
`rexruntime*.dll` from `out/install/win-amd64/bin/` over, and rebuild the game:
some of the SDK's start-up code is linked into the exe itself.

| Patch | |
|---|---|
| 0001 GPU-resident resolve data | the game's CPU readers of GPU results, without readback stalls |
| 0002 prompt vblank and wait polling | frames don't miss their refresh |
| 0003 vblank spin and pacing log | optional (off by default) |
| 0004 module unload and reload | re-entering World Cup mode |
| 0005 invalid call callers, quiet cores warning | diagnostics |
| 0006 texture changed while loading | missing letters in the game's text |
| 0007 GPU frame capture (F9, `gpu_frame_capture`) | debugging tool, off by default |
| 0008 keyboard: relaxed modifier matching (`mnk_exact_modifiers`) | Shift to sprint while steering |
| 0009 scaled resolves also written to guest memory | 1440p and 4K |
| 0010 disc image (.iso) as game data | playing from an `.iso` |
| 0011 present stall recovery (`d3d12_present_stall_recovery`) | FPS staying low after alt-tab or a screenshot popup |
| 0012 vpkd3d128 with the same source and destination | mouths stuck shut on one side, eyes not blinking |
| 0013 waitable swap chain (`d3d12_present_waitable`) | cutscene and pre-match frame drops at every resolution |
| 0014 MSAA clears drawn single-sample (`clear_msaa_redirect`) | fewer render target copies (1440p, 4K) |
| 0015 FSR 1 / CAS without the FidelityFX SDK | `present_effect = "fsr"` / `"cas"` |
| 0016 scaled resolves downscaled only when read (`defer_resolve_downscale`) | less GPU work at 1440p and 4K |
| 0017 new swap chain when the window is in front again | low frame rate for a while after alt-tab |
| 0018 the buttons the game last read (`InputSystem::GetLastGuestButtons`) | the game's quit shortcut |
| 0019 UTF-8 path settings read as UTF-8 | game data (or saves, cache) under a path with letters like é, ö, Cyrillic |
| 0020 the high-performance GPU first | laptops running the game on the integrated GPU |
| 0021 the config file opened by its wide path | Play opening the launcher again, for players whose Documents path has letters outside the system code page |
| 0022 why the guest stopped, logged | error 0x80000003 reports: the log (and the launcher's message) says which break it was |
| 0023 vblanks from a 60 Hz display, presented with vsync (`vsync_to_display`) | constant tearing and occasional hitches on 60 Hz screens |
| 0024 present spacing in the log | measuring how evenly frames reach the display |
| 0025 overlays without continuous repaint (`ImGuiDialog::WantsContinuousPaint`) | micro-stutter with G-SYNC / FreeSync |
| 0026 sleep instead of spinning | CPU running hot at a fraction of its total use |
| 0027 resolves written straight into the texture that reads them (`resolve_to_texture`) | less GPU work for the post-processing chain, mostly at 1440p and 4K |
| 0028 textures placed in shared heaps (`texture_heaps`) | hitches when a new screen appears (kit select, pre-match, replays) |
| 0029 8888 resolves read straight from the render target (`direct_resolve_32bpp`) | less GPU work in kit select, the pre-match and replays, mostly at 1440p and 4K |
| 0030 every resolve target texture created ready for 0027 | 0027 also in kit select and the pre-match |
| 0031 the scene's 2x MSAA tiles resolved straight from the render target (`direct_resolve_32bpp`) | less GPU work for the 3D scene, mostly at 1440p and 4K |

## 0001-gpu-resident-resolve-data

FIFA renders kit composites, shadow and stadium lighting maps on the GPU,
resolves them to memory and then processes them on the CPU (CRT `memcpy`,
XDK `XGUntileSurface`). Under emulation the newest resolve output lives only in
GPU memory, so the CPU sees stale or empty data: black stadium, dark shadows,
wrong kits. Xenia's answer is resolve readback (`readback_resolve`): `full` is
correct but stalls the GPU ~36 times a frame (~25 FPS here), and the delayed
modes hand the CPU the wrong pass.

This patch lets the game side handle it precisely instead (D3D12 backend):

| Piece | What it does |
|---|---|
| GPU-newer page tracking | `CommandProcessor` marks pages written by resolves, clears them when the CPU writes them (shared-memory global watch). `IGraphicsSystem::IsGpuMemoryNewer`. |
| GPU-side copies | `IGraphicsSystem::CopyGpuMemory`: queued to the command processor thread, executed in GPU memory (shared memory -> scratch -> shared memory), ordered before later guest GPU commands. |
| On-demand readback | `IGraphicsSystem::ReadbackGpuMemory`: blocks the calling guest thread until a range is read back, so a consumer pays one wait per surface, not per resolve. |
| `readback_probe` | Diagnostic cvar: protects every resolve destination and logs the first CPU function that touches it (symbolized from the PDB). This is how `memcpy` and `XGUntileSurface` were found. |
| `readback_resolve = "adaptive"` | Earlier experiment kept for comparison: sync readback for one-off resolves, delayed for per-frame ones. Faster than `full`, but renders darker here. |

GPU plugin ABI bumped to 2. The game-side hooks live in `pc/fixes/gpu_memcpy.cpp`.

Result on the stadium intro, readback off: 60 FPS (16.7 ms average, 18.5 ms p95),
lighting matching `full`.

## 0002-prompt-vblank-and-wait-polling

WAIT_REG_MEM slept `wait / 0x100` ms between polls (several ms for the game's
vblank waits), and the vblank timer thread woke once per millisecond. Either
can make a frame see its vblank late and miss the next refresh. Polls now
sleep 250 us, and the vblank thread sleeps until the next vblank is due.

## 0003-vblank-spin-and-pacing-log

`vsync_spin_us`: the vblank thread spins instead of sleeping in the last N
microseconds before a vblank, for systems where timed waits overshoot by a
whole OS timer tick (~15 ms) and vblanks come unevenly. Also logs vblank
spacing and the guest handler's time per 600 vblanks ("vblank pacing:").
Default 0 keeps the previous behaviour.

## 0004-module-unload-and-reload

Unloading a user module now unregisters the recompiled module it was wired
to at load time (remembered per module in `recompiled_module_keys_`).
Before, `UnloadUserModule` looked the recompiled module up again from the
loaded module's path, which is the resolved device path
(`\Device\Harddisk0\Partition1\dlc\...`) and doesn't normalize to the
recompiled module's guest path (`dlc/...`): the module left `user_modules_`
but its recompiled code stayed registered. FIFA unloads its World Cup DLC
module (`footballcompengzf.xex.dll`) when leaving World Cup mode and loads it
again on re-entry; the reload was refused ("already loaded; refusing
duplicate load", which now also logs the requested and loaded paths), the
load returned nothing and the game called address 0. Also: an already
loaded module is recognised by its normalized path as well as its exact
path.

## 0005-invalid-call-callers-and-quiet-cores-warning

A call through a null or unknown guest function pointer now logs who made it:
the caller (LR), r3/r30/r31 and the guest call stack (each frame's back chain
at 0(r1), the caller's LR saved at -8 of the frame above). That is what found
the heap set-up race in `pc/fixes/boot_race_fixes.cpp` from one crash on a
slow machine. "Too few processor cores - scheduling will be wonky" is logged
once instead of on every CPU switch (7,000 lines in a minute on a 4-thread
machine).

## 0006-texture-changed-while-loading

Letters went missing at random in the game's text (HUD, menus): the font's
glyph cache texture gains letters as they are first needed, written by a game
thread while the GPU thread may be loading that texture. The texture cache
copied the texture's pages from guest memory and only afterwards set up the
memory watches that notice CPU writes. A write in between invalidated the pages
(and lifted their write protection) with no watch registered yet, so the
texture was marked up to date with the old data and never reloaded: later
writes didn't fault any more. Now `Texture::MakeUpToDateAndWatch` checks
`SharedMemory::IsRangeValid` under the global lock and leaves a part that
changed during the load outdated, to be loaded again at the next use. Logged as
"changed by the CPU while loading". Xenia has the same code, which fits the
missing text reported there for several FIFA titles.

## 0007-gpu-frame-capture

A debugging tool for the D3D12 backend, off unless `gpu_frame_capture = true`.
F9 then captures the next frame: one log line per draw (whether it was drawn or
skipped while its pipeline compiled, shader hashes, render target and blend
state, cull/alpha-test/viewport state, the vertices of small draws, and every
texture it samples with the byte averages of its memory), the render targets
the draw was given and the ownership transfers into them, and for each resolve
the render targets it reads and its destination. Resolve outputs and large
sampled textures are written to `captures/NNN/*.bin`;
`tools/capture_to_png.py` untiles and converts them. Also logged without a
capture: how many draws are skipped while their pipelines compile.

This is how the black screen at launch and the black menu backgrounds were
found: stencil rectangles drawn with clipping off claimed the whole EDRAM
because the SDK estimates unclipped draws from the scissor unless
`execute_unclipped_draw_vs_on_cpu` is on (now set in the game's config).

## 0008-keyboard-relaxed-modifiers

The keyboard driver (`mnk_mode`) matches a keybind's Shift/Ctrl/Alt exactly,
so a key bound without them is dead while Shift is held, and Shift can't be
bound by itself: no "hold Shift to sprint while steering with WASD". With
`mnk_exact_modifiers = false`, a bind without modifiers works whatever
modifiers are held, a bind with them needs at least those, and a bare
modifier ("Shift") can be bound. Default true keeps the SDK's behaviour. The
launcher sets it with its keyboard layout (`launcher/src/settings.cpp`).

## 0009-scaled-resolve-to-guest-memory

With a draw resolution scale (`resolution_scale` 2 or 3: 1440p, 4K), a resolve
writes only the SDK's scaled resolve buffer, so guest memory keeps old data
where the game later reads its resolves at its own resolution: the readbacks
and GPU-side copies of the game's hooks (`pc/fixes/gpu_memcpy.cpp`: stadium
lighting, crowd colours, kits). Right after each scaled resolve,
`DownscaleResolveToSharedMemory` runs the SDK's `resolve_downscale` shader (the
one its readback path uses) and copies the result into shared memory at the
resolve's address; the pages are already GPU-written, so textures keep sampling
the scaled data. Without scaling nothing changes.

## 0010-disc-image-game-data

`game_data_root` may name a disc image file (`.iso`) instead of a folder:
`Runtime::SetupVfs` then mounts it with the SDK's existing `DiscImageDevice`
(Xenia's GDFX reader, read-only, memory-mapped) at the same place a folder goes
(`\Device\Harddisk0\Partition1`, linked as `game:` and `d:`), so the game sees
no difference. `ConstructRuntime` accepts a file there, and skips its host-side
`default.xex` check for an image (the XEX load fails with its own message if
it's missing). The reader also tries XGD1's partition offset (0x18300000), as
the launcher does.

## 0011-present-stall-recovery

After an overlay (ShareX's screenshot preview) or an alt-tab, Windows can leave
the swap chain on a path where every present waits for the display. The guest
then drops to ~48 FPS and stays there until the window is minimized and
restored. `D3D12Presenter` times each `Present`: when most of a block of 300
presents (the ones with the GPU not behind, so GPU-bound frames don't count)
take over 2 ms, it reports the connection outdated, and the UI thread's
reconnection creates a new swap chain, as restoring a minimized window does.
Most is 80%: in a heavy scene the GPU is behind for nearly all presents and the
few others are often slow too. If presents are slow again within 10 s of a new
swap chain, that one didn't help (most likely a scene too heavy for the GPU) and
none is created for 60 s.
It logs present times every 10 s. `d3d12_present_stall_recovery = false`
turns the recovery off.

## 0012-vpkd3d128-same-register

`vpkd3d128` packs floats into a D3D vertex format. The generated code writes the
destination register before it has read all of the source: the FLOAT16 cases
store each half and then read the sign from the source word. When the
destination is the source (`vpkd3d128 v63,v63,5,2,2`), the first value's sign
is read from the half just stored, so it always comes out positive. The game
does exactly that in its "write floats as half floats" routine
(`sub_825A3950`), which writes 16-bit float data such as vertex streams. It left
mouths stuck shut on one side and eyes twitching instead of blinking (players
and crowd), which Xenia doesn't show. Now, with the same register on both sides, the packing reads from
a copy. Two tests in `tests/ppc/asm/instr_vpkd3d128.s` fail without it.

## 0013-present-waitable

The presenter presents at the host refresh rate with `Present(0)`. Without
`DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` (variable refresh off, or not supported),
a Present with the frame queue full waits for the display, about 5 ms each at
144 Hz, and while it waits, the emulation's `ExecuteCommandLists` on the same
direct queue waits as well. Measured at 720p in the pre-match scenes: 5-13 ms
per guest frame in `ExecuteCommandLists`, the GPU idle 12-14 ms of a ~30 ms
frame, 28-36 FPS; at 4K 15-27 ms. The swap chain is now created with
`DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` (maximum frame latency 2),
and `PaintAndPresentImpl` waits on that object before painting, outside the
queue, so Present doesn't block. Same scenes with variable refresh off: 0.1-1
ms in `ExecuteCommandLists`, lowest second 52 FPS. `d3d12_present_waitable =
false` turns it off.

## 0014-clear-msaa-redirect

Direct3D 9 on the Xbox 360 clears a render target by drawing a rectangle with
its clear shaders, with 4x (or 2x) MSAA set on a half-size surface, which
covers the same EDRAM with a quarter of the pixels. On the host that is a
different render target, so each clear transferred the old contents into an
MSAA render target (overwritten at once) and the cleared result back for the
next single-sample draw: in this game 60-75% of all transferred data. Such a
draw (the clear vertex shader, the clear color pixel shader or none, a
rectangle list without clipping or window offset) is now drawn single-sample:
`ClearMsaaRedirect` doubles the surface pitch (4x), sets 1x MSAA and scales the
viewport and scissor for that draw, then restores the registers. A clear writes
one value everywhere, so the EDRAM ends up the same. At 4K: transfers per frame
23-37 -> 13, 4-6.5 -> 2-2.7 ms. `clear_msaa_redirect = false` turns it off.

## 0015-fsr-without-fidelityfx-sdk

The presenters' FSR 1 (EASU / RCAS) and CAS use shaders that are already built
into the tree, but `REX_HAS_FIDELITYFX_SDK`, which is only defined with the
FidelityFX SDK, turned them off along with FSR 2 / 3. The define is now always
set, publicly on `rexui` and `rexruntime`, as it changes the presenter classes'
layout for everything that includes them (rebuild the game after this one).
FSR 2 / 3 still need the SDK runtime (`REX_HAS_FIDELITYFX_RUNTIME`).

## 0016-defer-resolve-downscale

Patch 0009 downscaled every scaled resolve into shared memory right away, but
most are only ever sampled as scaled textures. Now they're queued
(`pending_downscales_`) and done when something reads the range from shared
memory: `SharedMemory::RequestRanges` calls a new `BeforeGpuRead` callback for
each range (index buffers, unscaled texture loads, GPU memory copies,
readbacks), textures loaded from the scaled resolve buffer suppress it, and a
draw's vertex buffers are checked before any of its state is bound (they're
requested after the bindings, where nothing may change the descriptor heap).
A newer resolve covering an older one's range replaces it; a CPU write to the
range drops it (the CPU's data replaces the resolve's there). At 4K about 1 ms
less GPU time per frame in play, with no visual change.
`defer_resolve_downscale = false` downscales right away again.

## 0017-present-recovery-on-refocus

0011's recovery also counted presents while the window was in the background,
where Windows throttles them and a new swap chain can't help; the failed
attempts then set its 60-second back-off, and coming back during it the frame
rate stayed low until it ran out (measured: ~30 s at 17-37 FPS after an
alt-tab at 4K). Presents with the window not in the foreground are no longer
counted, and when it's in front again a new swap chain is created right away,
as minimizing and restoring did.

## 0018-last-guest-buttons

`InputSystem::GetState` keeps the buttons of each user's last state (what the
game last read) in an atomic, and `GetLastGuestButtons` reads them from any
thread. The game's quit shortcut (`pc/src/quit_shortcut.cpp`: hold Back +
Start) uses it from the overlay; `GetStateForUI` isn't safe to call there, as
it isn't synchronised with the game's own polling.

## 0019-utf8-path-cvars

`ReXApp::SetupEnvironment` made the path cvars (`game_data_root`,
`user_data_root`, `update_data_root`, `cache_root`, `metadata_root`) into
paths with a plain `std::filesystem::path` from their UTF-8 text, which on
Windows reads it in the system's code page: a path with letters outside it
(é, ö, ç, Cyrillic, ...) then didn't exist. They go through `rex::to_path`
(UTF-8 to UTF-16) now. The game's own early read of `game_data_root`
(`pc/src/fifawc14_app.h`) does the same; that one sent players whose `.iso`
was under such a path straight back to the launcher on Play.

## 0020-high-performance-gpu

`D3D12Provider::Initialize` took the first adapter `EnumAdapters1` lists that
can make a Direct3D 12 device. That one drives the main display, on most
laptops the integrated GPU, so a laptop with an RTX 3080 Ti ran the game at
~20 FPS on its integrated graphics. With `d3d12_adapter = -1` (the default) it now
asks Windows for the high-performance GPU first
(`IDXGIFactory6::EnumAdapterByGpuPreference`), which also follows the per-app
choice in Windows' Graphics settings. An index, WARP (`-2`), or a Windows
without `IDXGIFactory6` (before 10 1803) choose as before. The SDK's
`NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance` exports are in
the GPU plugin DLL, where the drivers don't look; the game exports its own
(`pc/src/main.cpp`).

## 0021-config-file-wide-path

`LoadConfig` (`cvar.cpp`) and the log levels' read of the same file
(`logging.cpp`) opened it with `toml::parse_file(path.string())`. On Windows
`string()` is the system's code page, so with `fifawc14.toml` under a folder
with letters outside it (the user's Documents, under a Cyrillic or Greek user
name, say) the file couldn't be opened, or `string()` threw. The game then had
no `game_data_root` and opened the launcher again: to the player, Play opened
a second menu. 0019 fixed the paths read from the config; this one fixes how the
file is opened. Both now open it with `std::ifstream` on the wide path and parse
the stream, and log the path in UTF-8.

## 0022-guest-break-reason

The guest's `DbgBreakPoint`, a C++ exception it throws that nothing handles,
any other `RtlRaiseException`, and `KeBugCheck(Ex)` all end in
`rex::debug::Break()`, an `int3` nothing catches, so the process ends with
0x80000003 and the log said nothing about which one it was. Each now logs
`Guest stopped: <reason>` at error level and flushes the log first. The reason
is the thrown C++ type's decorated name, the exception code, or the bug check
code and parameters. The launcher shows that reason in its "closed
unexpectedly" message, since players send a screenshot of it more often than
the log.

## 0023-vsync-to-display

The guest's vblanks came from a timer, and every frame was presented at once
with tearing allowed (`Present(0, ALLOW_TEARING)`), which suits variable
refresh and high refresh rates. On a plain 60 Hz screen it tore constantly,
and the timer's 60 Hz and the display's (59.95, 60.02...) drifted apart, so
now and then a frame was shown twice or skipped: a hitch with the FPS counter
at 60. With `vsync_to_display` (default on) and the display within 1.5 Hz of
the guest's refresh rate, the vblank thread waits for the display's own
vertical blank (`IDXGIOutput::WaitForVBlank` on the swap chain's output) and
the presenter presents with vsync. The output and its refresh rate are looked
up every 120 presents, so moving the window or changing the mode switches
over. At other refresh rates, or when the output can't be found (some hybrid
laptops), it works as before. Present stall recovery (0011) skips presents
made with vsync, which wait for the display by design.

## 0024-present-spacing-log

Logs the time between presents every 600 presents ("present spacing:"):
p1, p50, p99, max, and how many were more than 2 ms off the median or over
1.5 times it. With variable refresh the display follows presents, so this is
what a player sees. A log of the old builds showed a 5.8 ms median: ~200
presents a second for a 60 FPS game (0025).

## 0025-overlays-without-continuous-repaint

`ImGuiDrawer` asked for another paint after every paint while any dialog
existed. The game always has three (its FPS counter and quit hint, and the
SDK's achievement toast dialog), so the window was repainted at the display's
full rate on top of the guest's 60 frames: ~200 presents a second at 144 Hz.
With G-SYNC or FreeSync the display then ran near its maximum refresh and
showed each guest frame for an uneven 2 or 3 refreshes, a micro-stutter with
the FPS counter at 60. A dialog now says whether it needs continuous repaints
(`WantsContinuousPaint`, default true); the achievement toast only while a
toast is queued or showing, and the game's overlays never, as they're drawn
with each guest frame. Measured at 144 Hz with FreeSync: the spacing median
went from 5.8 ms to 16.7 ms, with almost none off by more than 2 ms.

## 0026-sleep-instead-of-spinning

A laptop's CPU ran hot at ~40% use: several threads kept a core each at 100%
while only waiting. Measured with `log_thread_cpu` and the dev sampler:

- `rex::thread::Sleep` and `AlertableSleep` on Windows passed whole
  milliseconds to `::Sleep`, so anything under 1 ms became `Sleep(0)`, a
  yield. 0002's 250 us poll in WAIT_REG_MEM spun the GPU command thread at
  ~98%. Sub-20 ms sleeps now use a high-resolution waitable timer (Windows 10
  1803+, one per thread); without one, whole milliseconds rounded up.
- `XThread::Delay` (KeDelayExecutionThread) turned the guest's interval into
  milliseconds, so delays under 1 ms were yields too. It keeps microseconds.
- `TimerQueue`'s thread waited with disruptorplus' spin strategy, which mostly
  yields in a loop (~45% of a core). It polls with sleeps of up to 1 ms now: a
  new timer is seen within 1 ms, a due one on time.

The game's own spinning loops are hooked in the game (pc/src/frame_rate.cpp,
pc/fixes/ring_wait_sleep.cpp). Together: the GPU command thread ~98% -> ~25%,
the game's main thread 100% -> ~20%, its render thread 100% -> under 10%, the
timer thread ~45% -> idle, at the same frame pacing.

## 0027-resolve-to-texture

A resolve copies a render target into guest memory in the Xbox 360's tiled
layout, and the texture that samples it is then untiled from that memory again
and copied into place. At 4K that is four full-size passes per image: dump the
render target to the EDRAM buffer, resolve it to memory, untile it, copy it. The
game's post-processing chain (bloom, blur, the downsampled copies of the frame)
resolves 8888 images and samples them right back, every frame, to the same
addresses (counted in a test build: ~70% of the texture loads of resolved data
matched one resolve exactly).

The texture cache now keeps the latest resolve into each address. When a
texture loads exactly one such resolve's output (8888 from an 8888 render target
without MSAA, starting at its origin, same pitch and endianness, no larger than
the resolve), it remembers that texture. At the next resolve to that address, a
compute shader writes the render target region straight into the texture,
swapping red and blue when the resolve swapped them, and the texture is marked
up to date, so it isn't untiled again. The resolve to memory still happens, for
the CPU and for any other texture reading that memory; a CPU write still
invalidates the texture through its memory watch. Those textures are created
with UAV access.

Measured 2026-10-24 at 4K (RX 6700 XT): the 1280x720 8888 texture loads it
replaces were ~2 ms of a 27 ms pre-match frame (PIX), and in play, switched on
and off in the same scene, GPU time went from 11.9 to 11.6 ms per frame, the
texture loads from 19 to 9 per frame. No visual difference.
`resolve_to_texture = false` restores the old path and can be switched while the
game runs.

## 0028-texture-heaps

Every texture was a committed resource of its own, and creating one took
~0.35 ms (RX 6700 XT). A new screen creates 100-220 textures in one frame, so
the frame where kit select, the training intro or the pre-match appears spent
35-84 ms just creating them (measured 2026-10-24 with a hitch log).

Textures are now placed in 64 MB heaps (only non-render-target textures, not
zeroed), with a first-fit allocator per heap that merges free neighbours when a
texture is destroyed. The texture cache only destroys textures the GPU is done
with, so their memory is reused right away, behind an aliasing barrier; every
texture is written whole by its load before it's sampled. Empty heaps are
released, except one. Textures larger than a heap, and any allocation that
fails, are committed resources as before.

Measured: 166 textures created in 2.5 ms instead of ~55 ms; the 220-texture
transition no longer shows up as a slow frame. `texture_heaps = false` restores
committed resources.

## 0029-direct-resolve-32bpp

With host render targets a resolve is two compute passes: the render targets
owning the resolved EDRAM range are dumped into the EDRAM buffer, then the copy
shader reads the buffer and writes the destination in guest memory. The SDK's
`direct_host_resolve` doesn't skip the dump yet (`TryResolveCopyDirectly` falls
back to it). In kit select at 4K the dumps were 4.5 ms of the 6.8 ms of resolves
in a 19.6 ms frame (PIX, RX 6700 XT, 2026-10-24).

For the game's most common resolve (the fast 32bpp copy from an 8888 render
target without MSAA, one render target owning the range, its origin the
resolve's base tile) a variant of the fast copy shader reads the pixels from the
render target itself: `resolve_direct_32bpp_1x.xesli`, Xenia's
`resolve_fast_32bpp_1x2xmsaa` with the EDRAM buffer load replaced by a load from
an R8G8B8A8_UINT view of the render target, packed as in the EDRAM. The R/B swap,
the endian swap and the tiled destination address are Xenia's code unchanged.
The SDK's resolve shaders are Xenia's XeSL shaders as of `dfa1b3f` (compiled from
there they're identical to the SDK's bytecode), so the variant builds against
those headers (`fxc /T cs_5_1 /D SHADING_LANGUAGE_HLSL_XE=1`, with
`XE_RESOLVE_RESOLUTION_SCALED` for the scaled one). Same root signature and
constants as the fast copy.

Measured switching it live in the same scene (4K, `resolve_to_texture` off so
the memory it writes is what the textures load): kit select 19.3 -> 17.3 ms of
GPU time per frame (resolves 6.6 -> 5.2 ms), pre-match resolves ~6.5 -> ~4.4 ms;
no visual difference. `direct_resolve_32bpp = false` restores the dump.

## 0030-resolve-to-texture-uav-for-all-resolve-targets

0027 writes into a texture only if it was created with UAV access, and gave it
that only when an eligible resolve record for its address already existed. In
kit select the textures had been created earlier (before their resolves were
eligible, or for a resolve that wasn't), so they never got UAV access and were
untiled from memory every frame as before. Now every k_8_8_8_8 single-level 2D
texture whose memory a resolve has written (any record, or a scaled resolve) is
created with UAV access; which of them 0027 writes is decided as before.

Measured 2026-10-24 at 4K: textures written by resolves in kit select ~0 ->
~440 per second, its texture loads 4.1 -> ~2.5 ms per frame (14 -> 8), GPU
time per frame ~17.3 -> ~16.5 ms.

## 0031-direct-resolve-full-32bpp-2x

The game draws its 3D scene in two 640x720 tiles with 2x MSAA and resolves
each tile averaging both samples, which the SDK does with the full 32bpp resolve
(not the fast copy 0029 covers): the tiles were still dumped to the EDRAM buffer
first. `resolve_direct_full_32bpp_2x.xesli` is Xenia's `resolve_full_32bpp`
with the sample loads replaced by loads from the 8888 render target itself
(Texture2DMS, R8G8B8A8_UINT view): the selected guest sample, plus the other one
when averaging, as the host samples `GetD3D10SampleIndexForGuest2xMSAA` maps them
to (native 2x, or 2x emulated with 4x: one variant each, picked like the render
targets are). Averaging, exponent bias, R/B swap, packing to the destination
format, endian swap and the tiled address are Xenia's code unchanged. Same
conditions as 0029 otherwise (one render target owning the range, its origin
the resolve's base tile), under the same `direct_resolve_32bpp` switch.

Measured 2026-10-24 at 4K (live switch, `resolve_to_texture` off so the scene is
what the resolves wrote): resolves in kit select ~5.2 ms (0029 alone) -> ~4.0
ms, in the pre-match ~4.4 -> 3.4-4.0 ms; no visual difference, MSAA edges the
same.


## 0032-raise-host-thread-priority

Every few minutes the game dropped to ~50 FPS for a second or two, with bursts
of 50-120 ms frames. A system-wide trace showed the cause: background services
and apps waking up all at once (here the network stack, Tailscale and VS Code
reacting to Hyper-V rebuilding Windows Sandbox's container network every 2-5
minutes) took all 12 cores for tens of milliseconds. The GPU command thread
waits for the game's commands by yielding (`SwitchToThread`), and a thread
that yields goes back in line at its base priority, normal, without the boost
a thread woken from a wait gets; it sat ready but not running for up to 45 ms
while threads at priority 9-13 ran, and each frame, which passes from the
game's threads through it to the presenting thread, missed two or three
refreshes.

The process now runs at above-normal priority, and the presenting (main)
thread, the GPU command thread and the vblank thread at the highest priority
in it (12), above the normal-priority work of other programs and services.

Measured 2026-10-25 with Windows Sandbox enabled: before, every container
network rebuild during a match gave 5-20 frames over 40 ms; after, a 40 game
minute match with rebuilds during play had none.

## 0033-early-readbacks

FIFA builds each kit texture on the GPU, then reads it back and untiles it on
the CPU (`pc/fixes/gpu_memcpy.cpp`). Each readback stopped the game thread for
about 24 ms at 720p, twice a second while kits build (kit select, the training
intro), so those screens dropped frames at every resolution. Two waits made up
that time:

- The GPU command thread only took queued readbacks between batches of guest
  commands, ~13 ms later on average: it was usually inside a batch, waiting for
  a vblank or a fence. It now also takes the readbacks at the front of the queue
  during those waits (`ExecuteQueuedGpuMemoryReadbacks`). Readbacks only read
  what the GPU has done; copies, and readbacks queued behind them, keep their
  order.
- The readback itself waited for all work queued on the GPU, which runs a frame
  behind: 4-17 ms. The game reads each kit back a frame after resolving it, so a
  resolve into memory the game has read back before is now copied to a readback
  buffer right behind it, and that part of the frame is submitted at once
  (`readback_prefetch`, on by default). The readback then waits only for that
  submission and copies from its buffer. Any later GPU write to the range
  (resolve, GPU memory copy, memexport) or CPU write drops the early copy, and
  the readback falls back to the full wait.

Measured 2026-10-26 at 720p in kit select: 37 of 40 readbacks came from the
early copy, 0.8 ms each; the game thread's readback waiting went from 270-480 ms
to ~55 ms per 10 seconds, the worst single wait from 28.6 to 9.9 ms. Kits are
correct.

## 0034-pipeline-loading

On a Dell Latitude E7470 (Intel HD 520, 2 cores / 4 threads) the game showed a
black screen for 2-2.5 minutes at every start while it created the 190-odd
pipelines stored from earlier runs, and Windows itself stopped responding.

- The pipeline creation and shader translation threads run at the lowest
  priority within the game, whose other threads run above normal since 0032,
  so the rest of Windows keeps up with them. Three quarters of the logical
  processors still compile, but two are left free on small CPUs (the GPU
  command thread creates pipelines too): 2 threads instead of 3 on 4.
- `d3d12_pipeline_library` (on by default): the driver's compiled pipelines
  are kept between runs in an `ID3D12PipelineLibrary`, in
  `shaders/local/<title>.<rov|rtv>.d3d12.plib` beside the shareable storage.
  Each pipeline is named by an XXH3 of its whole description (the fixed state
  with pointers cleared, and every shader's bytecode), so one whose shaders or
  state changed gets a new entry instead of failing to load. The driver
  rejects a library from another driver or GPU; a new one is started then. It
  is saved from the storage write thread a few seconds after pipelines were
  added (the process can exit without shutting down), written beside the file
  and renamed over it.

Measured 2026-10-26 on the E7470: first start 2:40 (the library being built,
desktop responsive), later starts about 5 seconds of black screen.
