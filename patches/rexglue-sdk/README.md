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
