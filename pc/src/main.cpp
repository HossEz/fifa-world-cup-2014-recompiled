// fifawc14 - ReXGlue Recompiled Project

#include "generated/default/fifawc14_init.h"

#include "fifawc14_app.h"

#include <cstdint>

REX_DEFINE_APP(fifawc14, Fifawc14App::Create)

#ifdef _WIN32
// Laptops with two GPUs: NVIDIA's and AMD's drivers run a program on the
// discrete one when its exe exports these. The SDK's are in the GPU plugin
// DLL, where the drivers don't look (see SDK patch 0020, which also asks
// Windows for the high-performance GPU).
extern "C" {
__declspec(dllexport) uint32_t NvOptimusEnablement = 0x00000001;
__declspec(dllexport) uint32_t AmdPowerXpressRequestHighPerformance = 1;
}
#endif
