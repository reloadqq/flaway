#include <Windows.h>
#include <jni.h>
#include "flaway/flaway.h"
#include "flaway/hooks/Hook.h"
#include "flaway/utils/logger.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        logger::init(hModule);

        if (!Hook::init())
        {
            logger::log_error("Hook::init() failed");
            return FALSE;
        }

        logger::log("[dllmain] flaway loaded successfully");
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        if (!lpReserved)
        {
            flaway::instance->shutdown();
        }
    }
    return TRUE;
}
