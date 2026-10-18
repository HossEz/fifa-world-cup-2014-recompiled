# The launcher (Launcher.exe): a native Win32 + Direct3D 11 menu in the game's
# style, drawn with Dear ImGui. Its art and fonts are read at run time from the
# player's copy of the game; none ship with it.
#
#   cmake -S launcher -B launcher/out -G Ninja -DCMAKE_BUILD_TYPE=Release
#         -DREXSDK_DIR=<rexglue-sdk checkout>
#   cmake --build launcher/out
#
# Uses the SDK checkout only for its third-party sources (Dear ImGui, stb,
# libmspack's LZX decoder); the launcher doesn't link the game runtime.

cmake_minimum_required(VERSION 3.25)
# LLVM's resource compiler, when it's there. With clang-cl, CMake preprocesses
# the .rc with clang and hands it on in the form llvm-rc takes (its
# cmake_llvm_rc step); given Windows' rc.exe instead, that step hangs.
if(NOT CMAKE_RC_COMPILER)
  find_program(LLVM_RC llvm-rc HINTS "$ENV{ProgramFiles}/LLVM/bin")
  if(LLVM_RC)
    set(CMAKE_RC_COMPILER "${LLVM_RC}")
  endif()
endif()
project(launcher LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The shared C runtime, as the SDK's FFmpeg libraries (and the game) use.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
# Debug information (a PDB next to the exe) for the watchdog's stack traces.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
add_link_options(/DEBUG)

if(NOT EXISTS "${REXSDK_DIR}/thirdparty/imgui/imgui.h")
  message(FATAL_ERROR "Set REXSDK_DIR to the rexglue-sdk checkout")
endif()
set(TP "${REXSDK_DIR}/thirdparty")
set(IMGUI "${TP}/imgui")
set(MSPACK "${TP}/libmspack/libmspack/mspack")
# The SDK's FFmpeg (XMA2 decoder): headers from its source tree, libraries from
# its install (Release build).
set(REXSDK_INSTALL "${REXSDK_DIR}/out/install/win-amd64" CACHE PATH "Installed SDK")

add_executable(Launcher WIN32
  src/main.cpp
  src/about.cpp
  src/controllers.cpp
  src/audio.cpp
  src/game_check.cpp
  src/game_files.cpp
  src/game_source.cpp
  src/icon.cpp
  src/log.cpp
  src/settings.cpp
  src/setup.cpp
  src/textures.cpp
  src/updater.cpp
  src/ui.cpp
  src/watchdog.cpp
  ${IMGUI}/imgui.cpp
  ${IMGUI}/imgui_draw.cpp
  ${IMGUI}/imgui_tables.cpp
  ${IMGUI}/imgui_widgets.cpp
  ${IMGUI}/backends/imgui_impl_dx11.cpp
  ${IMGUI}/backends/imgui_impl_win32.cpp
  ${MSPACK}/lzxd.c
)
target_include_directories(Launcher PRIVATE src ${IMGUI} ${IMGUI}/backends ${TP}/stb ${MSPACK}
                                            ${TP}/FFmpeg
                                            # nlohmann/json (the update feed), carried by the SDK's inja
                                            ${TP}/inja/third_party/include)
target_compile_definitions(Launcher PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
                                            IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
# Which build this is, for the About screen: the version (../VERSION), the
# commit and the date. CMake runs again by itself when VERSION changes or a
# commit is made (it watches git's HEAD and the branch it points to), so a
# release built after its commit shows that commit.
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Properties, Details in Explorer: name, version, publisher.
include(${REPO_ROOT}/installer/version_resource.cmake)
add_version_resource(Launcher "2014 FIFA World Cup Brazil: Recompiled - Launcher" "Launcher.exe")
file(READ ${REPO_ROOT}/VERSION LAUNCHER_VERSION)
string(STRIP "${LAUNCHER_VERSION}" LAUNCHER_VERSION)
execute_process(COMMAND git rev-parse --short HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE LAUNCHER_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git rev-parse --absolute-git-dir
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_DIR OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git symbolic-ref -q HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_BRANCH_REF OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(GIT_DIR)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GIT_DIR}/HEAD)
  if(GIT_BRANCH_REF AND EXISTS ${GIT_DIR}/${GIT_BRANCH_REF})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GIT_DIR}/${GIT_BRANCH_REF})
  endif()
endif()
string(TIMESTAMP LAUNCHER_DATE "%d %B %Y" UTC)
set_source_files_properties(src/about.cpp PROPERTIES COMPILE_DEFINITIONS
  "LAUNCHER_VERSION=\"${LAUNCHER_VERSION}\";LAUNCHER_COMMIT=\"${LAUNCHER_COMMIT}\";LAUNCHER_DATE=\"${LAUNCHER_DATE}\"")

# Controllers through SDL3 (Xbox, PlayStation, Switch and others), as the game reads them.
target_include_directories(Launcher PRIVATE ${REXSDK_INSTALL}/include)
target_link_libraries(Launcher PRIVATE d3d11 dxgi windowscodecs ole32 shell32 xaudio2
                                       bcrypt winhttp ${REXSDK_INSTALL}/lib/libavcodec.lib
                                       ${REXSDK_INSTALL}/lib/libavutil.lib
                                       ${REXSDK_INSTALL}/lib/SDL3-static.lib
                                       dbghelp winmm imm32 oleaut32 version uuid advapi32 setupapi
                                       cfgmgr32 gdi32 user32 kernel32)
