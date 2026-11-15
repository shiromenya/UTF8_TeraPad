#include "utf8_util.h"

PFN_MultiByteToWideChar g_orig_MB2WC = NULL;
PFN_WideCharToMultiByte g_orig_WC2MB = NULL;
BOOL g_utf8_doc_active = FALSE;

static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_log_cs;
static BOOL g_log_cs_init = FALSE;

void tpad_log_init(HMODULE hSelf) {
    char env[8];
    if (GetEnvironmentVariableA("TPAD_UTF8_LOG", env, sizeof(env)) == 0 || env[0] != '1') {
        return;
    }
    InitializeCriticalSection(&g_log_cs);
    g_log_cs_init = TRUE;

    char log_path[MAX_PATH];
    DWORD n = GetModuleFileNameA(hSelf, log_path, MAX_PATH);
    while (n > 0 && log_path[n - 1] != '\\') n--;
    lstrcpyA(log_path + n, "tpad_utf8.log");

    g_log = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    tpad_log("=== tpad_utf8 log start ===\r\n");
}

void tpad_log_close(void) {
    if (g_log != INVALID_HANDLE_VALUE) {
        tpad_log("=== tpad_utf8 log end ===\r\n");
        CloseHandle(g_log);
        g_log = INVALID_HANDLE_VALUE;
    }
    if (g_log_cs_init) {
        DeleteCriticalSection(&g_log_cs);
        g_log_cs_init = FALSE;
    }
}

void tpad_log(const char *msg) {
    if (g_log == INVALID_HANDLE_VALUE || !msg) return;
    EnterCriticalSection(&g_log_cs);
    DWORD len = (DWORD)lstrlenA(msg);
    DWORD written = 0;
    WriteFile(g_log, msg, len, &written, NULL);
    LeaveCriticalSection(&g_log_cs);
}

void mem_copy(void *dst, const void *src, size_t n) {
    BYTE *d = (BYTE *)dst;
    const BYTE *s = (const BYTE *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

BOOL has_non_ascii(const char *s, int len) {
    if (!s) return FALSE;
    if (len < 0) {
        for (int i = 0; s[i] != '\0'; i++) {
            if ((unsigned char)s[i] >= 0x80) return TRUE;
        }
        return FALSE;
    }
    for (int i = 0; i < len; i++) {
        if ((unsigned char)s[i] >= 0x80) return TRUE;
    }
    return FALSE;
}

BOOL is_valid_utf8(const char *s, int len) {
    if (!s || len == 0 || !g_orig_MB2WC) return FALSE;
    return g_orig_MB2WC(65001, MB_ERR_INVALID_CHARS, s, len, NULL, 0) > 0;
}

BOOL has_non_sjis_unicode(const WCHAR *ws, int wlen) {
    if (!ws || wlen == 0 || !g_orig_WC2MB) return FALSE;
    BOOL used_def = FALSE;
    g_orig_WC2MB(932, 0x0400 /* WC_NO_BEST_FIT_CHARS */, ws, wlen, NULL, 0, NULL, &used_def);
    return used_def;
}

WCHAR *utf8_to_utf16_alloc(const char *s, int len, int *out_wlen) {
    if (!s || len == 0 || !g_orig_MB2WC) return NULL;
    int wlen = g_orig_MB2WC(65001, MB_ERR_INVALID_CHARS, s, len, NULL, 0);
    if (wlen <= 0) return NULL;
    WCHAR *wbuf = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (size_t)(wlen + 1) * sizeof(WCHAR));
    if (!wbuf) return NULL;
    g_orig_MB2WC(65001, MB_ERR_INVALID_CHARS, s, len, wbuf, wlen);
    wbuf[wlen] = L'\0';
    if (out_wlen) *out_wlen = wlen;
    return wbuf;
}

char *utf16_to_utf8_alloc(const WCHAR *ws, int wlen, int *out_len) {
    if (!ws || wlen == 0 || !g_orig_WC2MB) return NULL;
    int len = g_orig_WC2MB(65001, 0, ws, wlen, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *buf = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)len + 1);
    if (!buf) return NULL;
    g_orig_WC2MB(65001, 0, ws, wlen, buf, len, NULL, NULL);
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

void tpad_heap_free(void *ptr) {
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}
