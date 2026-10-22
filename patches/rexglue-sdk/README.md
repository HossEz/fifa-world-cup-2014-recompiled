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
