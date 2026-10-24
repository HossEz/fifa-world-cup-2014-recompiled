# Building

Everything here needs your own copy of *2014 FIFA World Cup Brazil* (Xbox
360), extracted to a folder with `default.xex`: the code generator reads the
game's executables. Nothing it produces from them is ever committed
(`pc/generated/` is ignored).

## Tools

Windows 10/11 x64, Visual Studio 2022 (C++ workload, for the Windows SDK and
linker), LLVM/clang, CMake 3.25+ and Ninja. Run the commands below from a
Visual Studio x64 developer prompt with LLVM first on `PATH`.

## 1. The ReXGlue SDK

Clone it at the commit the patches are made for, apply them, and build and
install **both** configurations:
[`patches/rexglue-sdk/README.md`](patches/rexglue-sdk/README.md).

The game's build runs code generation with the installed **Release**
`rexglue.exe` and links the **RelWithDebInfo** runtime, so both are needed. On
Windows the SDK's submodules may check out git symlinks as small text files;
replace them with copies of their targets.

## 2. The game

1. Point `pc/fifawc14_manifest.toml` at your copy (`game_root` and the three
   `file_path` entries).
2. Generate the C++ from the game's code (`pc/generated/`, about 450 files),
   with the SDK's installed code generator:

   ```
   cd pc
   <rexglue-sdk>\out\install\win-amd64\bin\rexglue.exe codegen fifawc14_manifest.toml
   ```

3. Build:

   ```
   cmake --preset win-amd64-relwithdebinfo -Drexglue_DIR=<rexglue-sdk>/out/install/win-amd64/lib/cmake/rexglue
   cmake --build out/build/win-amd64-relwithdebinfo
   ```

   The first build compiles all of the generated code, which takes a while;
   later builds rerun code generation by themselves when an input changes. For
   a release, use the `win-amd64-release` preset the same way.

4. To run it without the launcher, copy `pc/fifawc14.example.toml` next to
   `fifawc14.exe` as `fifawc14.toml` and set `game_data_root` to your copy
   (folder or `.iso`). The launcher writes this file itself, including the
   settings the game needs:

   | Setting | Why |
   |---|---|
   | `gpu_allow_invalid_fetch_constants = true` | The game leaves texture fetch type bits unset. |
   | `readback_resolve = "none"` | GPU results the game reads on the CPU are handled by `pc/fixes/gpu_memcpy.cpp`; reading back every resolve would stall the GPU. |
   | `protect_zero = false` | The game reads address 0, which the console allows. |
   | `execute_unclipped_draw_vs_on_cpu = true` | Without it, full-screen passes are assumed to cover all render memory, and parts of the menus render black. |
   | `async_shader_compilation = false` | A draw waits for its pipeline instead of being skipped while it compiles. Skipped, the ground outside the stadium stayed black on the first run (before the shader cache had its shaders). |
   | `d3d12_submit_on_primary_buffer_end = false` | One GPU submission per frame. Each submission waits while the GPU is busy, and the game waits on the emulation meanwhile: at 4K, frames that fit in time still missed the refresh. |
   | `clear_memory_page_state = false` | The GPU keeps the guest memory it already has from frame to frame. On (the SDK's default), every frame uploads it again in hundreds of small copies, each one draining the GPU first: a quarter to two fifths of the GPU time per frame. |
   | `present_interval_cap = 1`, `frame_rate_floor = 60` | 60 FPS in the menus and cutscenes the game holds at 30 and 40 (`pc/fixes/present_interval.cpp`, `pc/src/frame_rate.cpp`). |
   | `debug_keys = true` | Optional, for development: the SDK's overlays on F3 (debug), F4 (settings), F7 and the backtick key (console). |

## 3. The launcher

```
cmake -S launcher -B launcher/out -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
      -DREXSDK_DIR=<rexglue-sdk checkout>
cmake --build launcher/out
```

`Launcher.exe` looks for `fifawc14.exe` in its own folder;
`--game-folder <folder>` points it elsewhere.

## 4. A release

[`installer/README.md`](installer/README.md): the release folder, the portable
zip and the installer.
