#include "hooks.h"
#include "iat_hook.h"
#include "utf8_util.h"

typedef HANDLE  (WINAPI *PFN_GetClipboardData)(UINT);
typedef HANDLE  (WINAPI *PFN_SetClipboardData)(UINT, HANDLE);
typedef LONG    (WINAPI *PFN_ImmGetCompositionStringA)(HIMC, DWORD, LPVOID, DWORD);
typedef int     (WINAPI *PFN_GetWindowTextA)(HWND, LPSTR, int);
typedef BOOL    (WINAPI *PFN_SetWindowTextA)(HWND, LPCSTR);
typedef LRESULT (WINAPI *PFN_SendMessageA)(HWND, UINT, WPARAM, LPARAM);
typedef LRESULT (WINAPI *PFN_SendDlgItemMessageA)(HWND, int, UINT, WPARAM, LPARAM);

static PFN_GetClipboardData orig_GetClipboardData = NULL;
static PFN_SetClipboardData orig_SetClipboardData = NULL;
static PFN_ImmGetCompositionStringA orig_ImmGetCompositionStringA = NULL;
static PFN_GetWindowTextA orig_GetWindowTextA = NULL;
static PFN_SetWindowTextA orig_SetWindowTextA = NULL;
static PFN_SendMessageA orig_SendMessageA = NULL;
static PFN_SendDlgItemMessageA orig_SendDlgItemMessageA = NULL;

static HANDLE g_last_clip_utf8 = NULL;

static BOOL is_text_input_control(HWND hWnd) {
    char cls[64];
    if (!hWnd) return FALSE;
    if (GetClassNameA(hWnd, cls, sizeof(cls)) <= 0) return FALSE;
    return lstrcmpiA(cls, "Edit") == 0 ||
           lstrcmpiA(cls, "ComboBox") == 0 ||
           lstrcmpiA(cls, "TEdit") == 0 ||
           lstrcmpiA(cls, "TComboBox") == 0;
}

static int utf16_to_utf8_fit(const WCHAR *wbuf, int wlen, char *dst, int dst_cap) {
    if (!dst || dst_cap <= 0) return 0;
    if (!wbuf || wlen <= 0) {
        dst[0] = '\0';
        return 0;
    }
    int u8len = g_orig_WC2MB(65001, 0, wbuf, wlen, NULL, 0, NULL, NULL);
    if (u8len <= 0) {
        dst[0] = '\0';
        return 0;
    }
    if (u8len < dst_cap) {
        g_orig_WC2MB(65001, 0, wbuf, wlen, dst, dst_cap - 1, NULL, NULL);
        dst[u8len] = '\0';
        return u8len;
    }
    char *tmp = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)u8len);
    if (!tmp) {
        dst[0] = '\0';
        return 0;
    }
    g_orig_WC2MB(65001, 0, wbuf, wlen, tmp, u8len, NULL, NULL);
    int copy_len = dst_cap - 1;
    while (copy_len > 0 && ((unsigned char)tmp[copy_len] & 0xC0) == 0x80) {
        copy_len--;
    }
    mem_copy(dst, tmp, (size_t)copy_len);
    dst[copy_len] = '\0';
    tpad_heap_free(tmp);
    return copy_len;
}

static HANDLE WINAPI hook_GetClipboardData(UINT uFormat) {
    if (uFormat == CF_TEXT) {
        HANDLE hUni = orig_GetClipboardData(CF_UNICODETEXT);
        if (hUni) {
            const WCHAR *ws = (const WCHAR *)GlobalLock(hUni);
            if (ws) {
                int wlen = lstrlenW(ws);
                BOOL non_sjis = has_non_sjis_unicode(ws, wlen);
                if (non_sjis) {
                    g_utf8_doc_active = TRUE;
                }
                if (g_utf8_doc_active || non_sjis) {
                    int u8len = g_orig_WC2MB(65001, 0, ws, wlen + 1, NULL, 0, NULL, NULL);
                    if (u8len > 0) {
                        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)u8len);
                        if (hMem) {
                            char *dst = (char *)GlobalLock(hMem);
                            if (dst) {
                                g_orig_WC2MB(65001, 0, ws, wlen + 1, dst, u8len, NULL, NULL);
                                GlobalUnlock(hMem);
                                GlobalUnlock(hUni);
                                if (g_last_clip_utf8) {
                                    GlobalFree(g_last_clip_utf8);
                                }
                                g_last_clip_utf8 = hMem;
                                return hMem;
                            }
                            GlobalFree(hMem);
                        }
                    }
                }
                GlobalUnlock(hUni);
            }
        }
    }
    return orig_GetClipboardData(uFormat);
}

static HANDLE WINAPI hook_SetClipboardData(UINT uFormat, HANDLE hMem) {
    if (uFormat == CF_TEXT && hMem && g_utf8_doc_active) {
        const char *src = (const char *)GlobalLock(hMem);
        if (src) {
            int len = lstrlenA(src);
            if (has_non_ascii(src, len) && is_valid_utf8(src, len)) {
                int wlen = 0;
                WCHAR *ws = utf8_to_utf16_alloc(src, len + 1, &wlen);
                if (ws) {
                    HGLOBAL hUni = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wlen * sizeof(WCHAR));
                    if (hUni) {
                        WCHAR *dst = (WCHAR *)GlobalLock(hUni);
                        if (dst) {
                            mem_copy(dst, ws, (size_t)wlen * sizeof(WCHAR));
                            GlobalUnlock(hUni);
                            orig_SetClipboardData(CF_UNICODETEXT, hUni);
                        } else {
                            GlobalFree(hUni);
                        }
                    }
                    tpad_heap_free(ws);
                }
            }
            GlobalUnlock(hMem);
        }
    }
    return orig_SetClipboardData(uFormat, hMem);
}

static LONG WINAPI hook_ImmGetCompositionStringA(HIMC hIMC, DWORD dwIndex, LPVOID lpBuf, DWORD dwBufLen) {
    if ((dwIndex & (GCS_RESULTSTR | GCS_COMPSTR)) != 0) {
        LONG wbytes = ImmGetCompositionStringW(hIMC, dwIndex, NULL, 0);
        if (wbytes > 0) {
            WCHAR *wbuf = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (size_t)wbytes + sizeof(WCHAR));
            if (wbuf) {
                LONG got = ImmGetCompositionStringW(hIMC, dwIndex, wbuf, (DWORD)wbytes);
                if (got > 0) {
                    int actual_wlen = (int)(got / (LONG)sizeof(WCHAR));
                    wbuf[actual_wlen] = L'\0';
                    BOOL non_sjis = has_non_sjis_unicode(wbuf, actual_wlen);
                    if (non_sjis) g_utf8_doc_active = TRUE;

                    if (g_utf8_doc_active || non_sjis) {
                        int u8len = g_orig_WC2MB(65001, 0, wbuf, actual_wlen, NULL, 0, NULL, NULL);
                        if (u8len > 0) {
                            if (dwBufLen > 0 && lpBuf) {
                                int copy_len = ((DWORD)u8len <= dwBufLen) ? u8len : (int)dwBufLen;
                                g_orig_WC2MB(65001, 0, wbuf, actual_wlen, (LPSTR)lpBuf, copy_len, NULL, NULL);
                                tpad_heap_free(wbuf);
                                return copy_len;
                            }
                            tpad_heap_free(wbuf);
                            return u8len;
                        }
                    }
                }
                tpad_heap_free(wbuf);
            }
        }
    }
    return orig_ImmGetCompositionStringA(hIMC, dwIndex, lpBuf, dwBufLen);
}

static int WINAPI hook_GetWindowTextA(HWND hWnd, LPSTR lpString, int nMaxCount) {
    if (is_main_thread() && g_utf8_doc_active && lpString && nMaxCount > 1) {
        WCHAR *wbuf = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (size_t)nMaxCount * sizeof(WCHAR));
        if (wbuf) {
            int wlen = GetWindowTextW(hWnd, wbuf, nMaxCount);
            if (wlen > 0 && (is_text_input_control(hWnd) || has_non_sjis_unicode(wbuf, wlen))) {
                int u8len = utf16_to_utf8_fit(wbuf, wlen, lpString, nMaxCount);
                tpad_heap_free(wbuf);
                return u8len;
            }
            tpad_heap_free(wbuf);
        }
    }
    return orig_GetWindowTextA(hWnd, lpString, nMaxCount);
}

static BOOL WINAPI hook_SetWindowTextA(HWND hWnd, LPCSTR lpString) {
    if (is_main_thread() && lpString) {
        /* Detect "無題" (Shift_JIS: 96 B3 91 E8) in window title -> new empty document */
        const unsigned char *u = (const unsigned char *)lpString;
        if (u[0] == 0x96 && u[1] == 0xB3 && u[2] == 0x91 && u[3] == 0xE8) {
            g_utf8_doc_active = TRUE;
        } else if (g_utf8_doc_active && has_non_ascii(lpString, -1) && is_valid_utf8(lpString, -1)) {
            int wlen = 0;
            WCHAR *wbuf = utf8_to_utf16_alloc(lpString, -1, &wlen);
            if (wbuf) {
                BOOL ok = SetWindowTextW(hWnd, wbuf);
                tpad_heap_free(wbuf);
                return ok;
            }
        }
    }
    return orig_SetWindowTextA(hWnd, lpString);
}

static LRESULT WINAPI hook_SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (is_main_thread() && g_utf8_doc_active) {
        switch (Msg) {
        case WM_SETTEXT: {
            const char *s = (const char *)lParam;
            if (is_text_input_control(hWnd) && s && has_non_ascii(s, -1) && is_valid_utf8(s, -1)) {
                int wlen = 0;
                WCHAR *wbuf = utf8_to_utf16_alloc(s, -1, &wlen);
                if (wbuf) {
                    LRESULT ret = SendMessageW(hWnd, Msg, wParam, (LPARAM)wbuf);
                    tpad_heap_free(wbuf);
                    return ret;
                }
            }
            break;
        }
        case CB_ADDSTRING:
        case CB_INSERTSTRING:
        case CB_FINDSTRING:
        case CB_FINDSTRINGEXACT:
        case CB_SELECTSTRING: {
            const char *s = (const char *)lParam;
            if (s && has_non_ascii(s, -1) && is_valid_utf8(s, -1)) {
                int wlen = 0;
                WCHAR *wbuf = utf8_to_utf16_alloc(s, -1, &wlen);
                if (wbuf) {
                    LRESULT ret = SendMessageW(hWnd, Msg, wParam, (LPARAM)wbuf);
                    tpad_heap_free(wbuf);
                    return ret;
                }
            }
            break;
        }
        case WM_GETTEXTLENGTH: {
            if (is_text_input_control(hWnd)) {
                LRESULT wlen = SendMessageW(hWnd, WM_GETTEXTLENGTH, 0, 0);
                if (wlen <= 0) return wlen;
                WCHAR *wbuf = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (size_t)(wlen + 1) * sizeof(WCHAR));
                if (wbuf) {
                    LRESULT actual_wlen = SendMessageW(hWnd, WM_GETTEXT, (WPARAM)(wlen + 1), (LPARAM)wbuf);
                    int u8len = (actual_wlen > 0) ? g_orig_WC2MB(65001, 0, wbuf, (int)actual_wlen, NULL, 0, NULL, NULL) : 0;
                    tpad_heap_free(wbuf);
                    return (LRESULT)u8len;
                }
            }
            break;
        }
        case WM_GETTEXT: {
            if (is_text_input_control(hWnd) && wParam > 0 && lParam) {
                LRESULT wlen = SendMessageW(hWnd, WM_GETTEXTLENGTH, 0, 0);
                if (wlen <= 0) {
                    ((char *)lParam)[0] = '\0';
                    return 0;
                }
                WCHAR *wbuf = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (size_t)(wlen + 1) * sizeof(WCHAR));
                if (wbuf) {
                    LRESULT actual_wlen = SendMessageW(hWnd, WM_GETTEXT, (WPARAM)(wlen + 1), (LPARAM)wbuf);
                    int u8len = utf16_to_utf8_fit(wbuf, (int)actual_wlen, (char *)lParam, (int)wParam);
                    tpad_heap_free(wbuf);
                    return (LRESULT)u8len;
                }
            }
            break;
        }
        case CB_GETLBTEXTLEN: {
            LRESULT wlen = SendMessageW(hWnd, CB_GETLBTEXTLEN, wParam, 0);
            if (wlen <= 0) return wlen;
            WCHAR *wbuf = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (size_t)(wlen + 1) * sizeof(WCHAR));
            if (wbuf) {
                LRESULT actual_wlen = SendMessageW(hWnd, CB_GETLBTEXT, wParam, (LPARAM)wbuf);
                int u8len = (actual_wlen > 0) ? g_orig_WC2MB(65001, 0, wbuf, (int)actual_wlen, NULL, 0, NULL, NULL) : 0;
                tpad_heap_free(wbuf);
                return (LRESULT)u8len;
            }
            break;
        }
        case CB_GETLBTEXT: {
            if (lParam) {
                LRESULT wlen = SendMessageW(hWnd, CB_GETLBTEXTLEN, wParam, 0);
                if (wlen <= 0) {
                    ((char *)lParam)[0] = '\0';
                    return wlen;
                }
                WCHAR *wbuf = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (size_t)(wlen + 1) * sizeof(WCHAR));
                if (wbuf) {
                    LRESULT actual_wlen = SendMessageW(hWnd, CB_GETLBTEXT, wParam, (LPARAM)wbuf);
                    if (actual_wlen > 0) {
                        int u8len = g_orig_WC2MB(65001, 0, wbuf, (int)actual_wlen, NULL, 0, NULL, NULL);
                        if (u8len > 0) {
                            g_orig_WC2MB(65001, 0, wbuf, (int)actual_wlen, (LPSTR)lParam, u8len, NULL, NULL);
                            ((char *)lParam)[u8len] = '\0';
                            tpad_heap_free(wbuf);
                            return (LRESULT)u8len;
                        }
                    }
                    tpad_heap_free(wbuf);
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return orig_SendMessageA(hWnd, Msg, wParam, lParam);
}

static LRESULT WINAPI hook_SendDlgItemMessageA(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam) {
    HWND hCtrl = GetDlgItem(hDlg, nIDDlgItem);
    if (hCtrl) {
        return hook_SendMessageA(hCtrl, Msg, wParam, lParam);
    }
    return orig_SendDlgItemMessageA(hDlg, nIDDlgItem, Msg, wParam, lParam);
}

void install_input_hooks(HMODULE exe) {
    iat_hook_module(exe, "user32.dll", "GetClipboardData", hook_GetClipboardData, (void **)&orig_GetClipboardData);
    iat_hook_module(exe, "user32.dll", "SetClipboardData", hook_SetClipboardData, (void **)&orig_SetClipboardData);
    iat_hook_module(exe, "user32.dll", "GetWindowTextA", hook_GetWindowTextA, (void **)&orig_GetWindowTextA);
    iat_hook_module(exe, "user32.dll", "SetWindowTextA", hook_SetWindowTextA, (void **)&orig_SetWindowTextA);
    iat_hook_module(exe, "user32.dll", "SendMessageA", hook_SendMessageA, (void **)&orig_SendMessageA);
    iat_hook_module(exe, "user32.dll", "SendDlgItemMessageA", hook_SendDlgItemMessageA, (void **)&orig_SendDlgItemMessageA);
    iat_hook_module(exe, "imm32.dll", "ImmGetCompositionStringA", hook_ImmGetCompositionStringA, (void **)&orig_ImmGetCompositionStringA);
}
