// Kernel exports FIFA imports that the ReXGlue SDK does not implement yet.

#include <rex/hook.h>
#include <rex/system/xtypes.h>

using rex::X_RESULT;

// Xbox Live Vision camera: report "no camera attached" so the game skips camera features.
REX_EXPORT_STUB_RETURN(__imp__XUsbcamGetState, X_ERROR_DEVICE_NOT_CONNECTED);
REX_EXPORT_STUB_RETURN(__imp__XUsbcamSetConfig, X_ERROR_DEVICE_NOT_CONNECTED);
