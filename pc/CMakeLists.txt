# Windows build of the recompiled game (ReXGlue project).
#
# This folder is also where code generation runs: generated/ (local only, never
# committed) holds the C++ translated from the game. SDK boilerplate lives in
# generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
# LLVM's resource compiler, when it's there, as in the launcher: given Windows'
# rc.exe, CMake's clang preprocessing step for the .rc hangs.
if(NOT CMAKE_RC_COMPILER)
  find_program(LLVM_RC llvm-rc HINTS "$ENV{ProgramFiles}/LLVM/bin")
  if(LLVM_RC)
    set(CMAKE_RC_COMPILER "${LLVM_RC}")
  endif()
endif()
project(fifawc14 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Target AVX2-era CPUs (Intel Haswell / AMD Zen and newer) instead of the SSE4.1
# baseline: the recompiled VMX128 vector code benefits most.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    add_compile_options(-march=x86-64-v3)
endif()

# Release builds keep line tables (function names and line numbers, no
# variables) in a PDB beside each binary, for reading players' crash reports.
# The code is the same; the PDBs go in the private symbols zip, not the release
# (installer/package.ps1). /OPT:REF,ICF as without /DEBUG, so the size stays.
if(WIN32)
    add_compile_options($<$<CONFIG:Release>:-gline-tables-only> $<$<CONFIG:Release>:-gcodeview>)
    add_link_options($<$<CONFIG:Release>:LINKER:/DEBUG> $<$<CONFIG:Release>:LINKER:/OPT:REF>
                     $<$<CONFIG:Release>:LINKER:/OPT:ICF>)
endif()

include(generated/rexglue.cmake)

# Sources: the Windows app (src/) and the game fixes (fixes/).
set(FIXES ${CMAKE_CURRENT_SOURCE_DIR}/fixes)
set(FIFAWC14_SOURCES
    src/main.cpp
    src/data_folder.cpp
    src/fps_counter.cpp
    src/quit_shortcut.cpp
    src/frame_rate.cpp
    src/launcher_redirect.cpp
    src/thread_stats.cpp
    src/game_window.cpp
    ${FIXES}/kernel_stubs.cpp
    ${FIXES}/gpu_memcpy.cpp
    ${FIXES}/boot_race_fixes.cpp
    ${FIXES}/present_interval.cpp
    ${FIXES}/lua_errors.cpp
)

if(WIN32)
    add_executable(fifawc14 WIN32 ${FIFAWC14_SOURCES})
else()
    add_executable(fifawc14 ${FIFAWC14_SOURCES})
endif()

rexglue_setup_target(fifawc14 GPU_PLUGINS xenos)
# Properties, Details in Explorer: name, version, publisher.
include(${CMAKE_CURRENT_SOURCE_DIR}/../installer/version_resource.cmake)
add_version_resource(fifawc14 "2014 FIFA World Cup Brazil: Recompiled - Game" "fifawc14.exe")
# The game window's 16:9 resizing hooks the window with SetWindowSubclass.
if(WIN32)
    target_link_libraries(fifawc14 PRIVATE comctl32)
endif()
