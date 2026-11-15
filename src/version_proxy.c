#include <windows.h>

static HMODULE g_real_version = NULL;

#define PROXY_LIST(X) \
    X(GetFileVersionInfoA) \
    X(GetFileVersionInfoByHandle) \
    X(GetFileVersionInfoExA) \
    X(GetFileVersionInfoExW) \
    X(GetFileVersionInfoSizeA) \
    X(GetFileVersionInfoSizeExA) \
    X(GetFileVersionInfoSizeExW) \
    X(GetFileVersionInfoSizeW) \
    X(GetFileVersionInfoW) \
    X(VerFindFileA) \
    X(VerFindFileW) \
    X(VerInstallFileA) \
    X(VerInstallFileW) \
    X(VerLanguageNameA) \
    X(VerLanguageNameW) \
    X(VerQueryValueA) \
    X(VerQueryValueW)

#define DEF_PTR(name) static FARPROC p_##name = NULL;
PROXY_LIST(DEF_PTR)
#undef DEF_PTR

BOOL version_proxy_init(void) {
    if (g_real_version) return TRUE;
    WCHAR sysdir[MAX_PATH];
    UINT len = GetSystemDirectoryW(sysdir, MAX_PATH);
    if (len == 0 || len + 13 >= MAX_PATH) return FALSE;
    const WCHAR suffix[] = L"\\version.dll";
    for (UINT i = 0; i < sizeof(suffix) / sizeof(WCHAR); i++) {
        sysdir[len + i] = suffix[i];
    }
    g_real_version = LoadLibraryW(sysdir);
    if (!g_real_version) return FALSE;

#define LOAD_PTR(name) p_##name = GetProcAddress(g_real_version, #name);
    PROXY_LIST(LOAD_PTR)
#undef LOAD_PTR
    return TRUE;
}

#define DEF_STUB(name) \
    __attribute__((naked)) void __cdecl proxy_##name(void) { \
        __asm__ volatile ("jmp *%0" : : "m"(p_##name)); \
    }

PROXY_LIST(DEF_STUB)
#undef DEF_STUB
