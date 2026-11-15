#include <windows.h>
#include "hooks.h"
#include "utf8_util.h"

BOOL version_proxy_init(void);

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        if (!version_proxy_init()) return FALSE;
        g_main_thread_id = GetCurrentThreadId();
        tpad_log_init(hinstDLL);
        HMODULE exe = GetModuleHandleA(NULL);
        install_text_hooks(exe);
        install_input_hooks(exe);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        cleanup_text_hooks();
        tpad_log_close();
    }
    return TRUE;
}
