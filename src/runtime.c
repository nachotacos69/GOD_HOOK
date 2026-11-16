#include <pspiofilemgr.h>
#include <psptypes.h>

#include "runtime.h"

#define EMULATOR_DEVCTL_IS_EMULATOR 0x00000003

static int g_ppsspp;

int geb_runtime_detect(void)
{
    u32 detected = 0;
    int result = sceIoDevctl("kemulator:", EMULATOR_DEVCTL_IS_EMULATOR,
        0, 0, &detected, sizeof(detected));
    g_ppsspp = result == 0 && detected == 1;
    return g_ppsspp;
}

int geb_runtime_is_ppsspp(void)
{
    return g_ppsspp;
}
