#include "hooks.h"
#include "iat_hook.h"
#include "utf8_util.h"

#define TPAD_SYNTH_LPARAM 0x00FF0001L
#define SEG_CACHE_CAP 64
#define SEG_MAX_BYTES 256

typedef BOOL (WINAPI *PFN_ExtTextOutA)(HDC, int, int, UINT, const RECT *, LPCSTR, UINT, const INT *);
typedef BOOL (WINAPI *PFN_GetTextExtentPoint32A)(HDC, LPCSTR, int, LPSIZE);
typedef BOOL (WINAPI *PFN_GetTextExtentPointA)(HDC, LPCSTR, int, LPSIZE);
typedef int  (WINAPI *PFN_DrawTextA)(HDC, LPCSTR, int, LPRECT, UINT);
typedef BOOL (WINAPI *PFN_SetCaretPos)(int, int);

static PFN_ExtTextOutA orig_ExtTextOutA = NULL;
static PFN_GetTextExtentPoint32A orig_GetTextExtentPoint32A = NULL;
static PFN_GetTextExtentPointA orig_GetTextExtentPointA = NULL;
static PFN_DrawTextA orig_DrawTextA = NULL;
static PFN_SetCaretPos orig_SetCaretPos = NULL;

typedef struct {
    int x;
    int y;
    int total_w;
    int len;
    BYTE bytes[SEG_MAX_BYTES];
    INT dx[SEG_MAX_BYTES];
} DrawSeg;

static DrawSeg g_seg_cache[SEG_CACHE_CAP];
static int g_seg_head = 0;

static HWND g_editor_hwnd = NULL;
static WNDPROC g_orig_editor_wndproc = NULL;

static int g_del_pending = 0;
static WPARAM g_del_vk = 0;
static int g_nav_pending = 0;
static WPARAM g_nav_vk = 0;
static BOOL g_paint_had_invalid = FALSE;
static BOOL g_last_paint_had_invalid = FALSE;

static void record_draw_segment(int x, int y, const char *str, UINT c, const INT *dx) {
    if (!str || !dx || c == 0) return;
    DrawSeg *seg = &g_seg_cache[g_seg_head];
    g_seg_head = (g_seg_head + 1) % SEG_CACHE_CAP;

    int n = (c > SEG_MAX_BYTES) ? SEG_MAX_BYTES : (int)c;
    int total = 0;
    for (int i = 0; i < n; i++) {
        seg->bytes[i] = (BYTE)str[i];
        seg->dx[i] = dx[i];
        total += dx[i];
    }
    seg->x = x;
    seg->y = y;
    seg->total_w = total;
    seg->len = n;
}

static BOOL is_caret_on_utf8_cont(int cx, int cy) {
    for (int step = 1; step <= SEG_CACHE_CAP; step++) {
        int idx = (g_seg_head - step + SEG_CACHE_CAP) % SEG_CACHE_CAP;
        const DrawSeg *seg = &g_seg_cache[idx];
        if (seg->len <= 0) continue;
        int dy = cy - seg->y;
        if (dy < -4 || dy > 4) continue;
        if (cx < seg->x || cx >= seg->x + seg->total_w) continue;

        int px = seg->x;
        for (int i = 0; i < seg->len; i++) {
            if (cx == px || (cx > px && cx < px + seg->dx[i])) {
                return (i > 0 && (seg->bytes[i] & 0xC0) == 0x80);
            }
            px += seg->dx[i];
        }
    }
    return FALSE;
}

static LRESULT CALLBACK editor_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        g_paint_had_invalid = FALSE;
        LRESULT res = CallWindowProcA(g_orig_editor_wndproc, hwnd, msg, wParam, lParam);
        g_last_paint_had_invalid = g_paint_had_invalid;
        return res;
    }
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_BACK || wParam == VK_DELETE) {
            g_nav_pending = 0;
            if (lParam != TPAD_SYNTH_LPARAM) {
                g_del_vk = wParam;
                g_del_pending = (g_utf8_doc_active && !g_last_paint_had_invalid) ? 3 : 0;
            }
        } else if (wParam == VK_LEFT || wParam == VK_RIGHT) {
            g_del_pending = 0;
            if (lParam != TPAD_SYNTH_LPARAM) {
                g_nav_vk = wParam;
                g_nav_pending = g_utf8_doc_active ? 3 : 0;
            }
        } else if (wParam != VK_SHIFT && wParam != VK_CONTROL && wParam != VK_MENU) {
            g_del_pending = 0;
            g_nav_pending = 0;
        }
    } else if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN) {
        g_del_pending = 0;
        g_nav_pending = 0;
    } else if (msg == WM_NCDESTROY) {
        WNDPROC orig = g_orig_editor_wndproc;
        if (hwnd == g_editor_hwnd) {
            g_editor_hwnd = NULL;
            g_orig_editor_wndproc = NULL;
            SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)(LONG_PTR)orig);
        }
        return CallWindowProcA(orig, hwnd, msg, wParam, lParam);
    }
    return CallWindowProcA(g_orig_editor_wndproc, hwnd, msg, wParam, lParam);
}

static void ensure_editor_subclassed(HDC hdc) {
    if (g_editor_hwnd != NULL) return;
    HWND hwnd = WindowFromDC(hdc);
    if (hwnd) {
        WNDPROC cur = (WNDPROC)(LONG_PTR)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)(LONG_PTR)editor_wndproc);
        if (cur && cur != editor_wndproc) {
            g_editor_hwnd = hwnd;
            g_orig_editor_wndproc = cur;
        }
    }
}

static int utf8_seq_len(unsigned char b) {
    if ((b & 0x80) == 0) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

static int WINAPI hook_MultiByteToWideChar(UINT cp, DWORD flags, LPCCH mb, int cb_mb, LPWSTR wc, int cch_wc) {
    if (cp == 65001) {
        g_utf8_doc_active = TRUE;
    } else if (cp == 51932 || cp == 50220 || cp == 50222 || cp == 1200 || cp == 1201) {
        g_utf8_doc_active = FALSE;
    }

    if (g_utf8_doc_active && (cp == 0 || cp == 932) && mb && cb_mb != 0) {
        if (has_non_ascii(mb, cb_mb)) {
            int wlen = g_orig_MB2WC(65001, MB_ERR_INVALID_CHARS, mb, cb_mb, wc, cch_wc);
            if (wlen > 0) return wlen;
        }
    }
    return g_orig_MB2WC(cp, flags, mb, cb_mb, wc, cch_wc);
}

static int WINAPI hook_WideCharToMultiByte(UINT cp, DWORD flags, LPCWCH wc, int cch_wc, LPSTR mb, int cb_mb, LPCCH def, LPBOOL used_def) {
    if (g_utf8_doc_active && (cp == 0 || cp == 932)) {
        if (used_def) *used_def = FALSE;
        return g_orig_WC2MB(65001, 0, wc, cch_wc, mb, cb_mb, NULL, NULL);
    }
    return g_orig_WC2MB(cp, flags, wc, cch_wc, mb, cb_mb, def, used_def);
}

static BOOL WINAPI hook_ExtTextOutA(HDC hdc, int x, int y, UINT opt, const RECT *lprc, LPCSTR str, UINT c, const INT *dx) {
    if (dx != NULL) {
        ensure_editor_subclassed(hdc);
        if (g_utf8_doc_active && str && c > 0) {
            record_draw_segment(x, y, str, c, dx);
        }
    }
    if (g_utf8_doc_active && str && c > 0 && has_non_ascii(str, (int)c)) {
        int wlen = 0;
        WCHAR *wbuf = utf8_to_utf16_alloc(str, (int)c, &wlen);
        if (!wbuf && dx != NULL) {
            g_paint_had_invalid = TRUE;
            if (g_del_pending > 0 && g_editor_hwnd && g_del_vk) {
                g_del_pending--;
                PostMessageA(g_editor_hwnd, WM_KEYDOWN, g_del_vk, TPAD_SYNTH_LPARAM);
            }
            int flen = g_orig_MB2WC(65001, 0, str, (int)c, NULL, 0);
            if (flen > 0) {
                wbuf = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (size_t)(flen + 1) * sizeof(WCHAR));
                if (wbuf) {
                    g_orig_MB2WC(65001, 0, str, (int)c, wbuf, flen);
                    wbuf[flen] = L'\0';
                    wlen = flen;
                }
            }
        }
        if (wbuf) {
            INT *dx_w = NULL;
            if (dx) {
                dx_w = (INT *)HeapAlloc(GetProcessHeap(), 0, (size_t)wlen * sizeof(INT));
                if (dx_w) {
                    int wi = 0;
                    for (UINT bi = 0; bi < c && wi < wlen; ) {
                        int seq = utf8_seq_len((unsigned char)str[bi]);
                        int sum_dx = 0;
                        for (int k = 0; k < seq && (bi + (UINT)k) < c; k++) {
                            sum_dx += dx[bi + (UINT)k];
                        }
                        if (seq == 4 && wi + 1 < wlen) {
                            dx_w[wi++] = sum_dx;
                            dx_w[wi++] = 0;
                        } else {
                            dx_w[wi++] = sum_dx;
                        }
                        bi += (UINT)seq;
                    }
                }
            }
            BOOL ok = ExtTextOutW(hdc, x, y, opt, lprc, wbuf, (UINT)wlen, dx_w);
            tpad_heap_free(wbuf);
            tpad_heap_free(dx_w);
            return ok;
        }
    }
    return orig_ExtTextOutA(hdc, x, y, opt, lprc, str, c, dx);
}

static BOOL WINAPI hook_SetCaretPos(int X, int Y) {
    if (g_utf8_doc_active && g_nav_pending > 0 && g_editor_hwnd && g_nav_vk) {
        if (is_caret_on_utf8_cont(X, Y)) {
            g_nav_pending--;
            PostMessageA(g_editor_hwnd, WM_KEYDOWN, g_nav_vk, TPAD_SYNTH_LPARAM);
        } else {
            g_nav_pending = 0;
        }
    }
    return orig_SetCaretPos(X, Y);
}

static BOOL hook_GetTextExtentPoint_common(HDC hdc, LPCSTR str, int c, LPSIZE psizl, BOOL is_32) {
    if (g_utf8_doc_active && str && c > 0 && has_non_ascii(str, c)) {
        int wlen = 0;
        WCHAR *wbuf = utf8_to_utf16_alloc(str, c, &wlen);
        if (wbuf) {
            BOOL ok = GetTextExtentPoint32W(hdc, wbuf, wlen, psizl);
            tpad_heap_free(wbuf);
            return ok;
        }
    }
    return is_32 ? orig_GetTextExtentPoint32A(hdc, str, c, psizl)
                 : orig_GetTextExtentPointA(hdc, str, c, psizl);
}

static BOOL WINAPI hook_GetTextExtentPoint32A(HDC hdc, LPCSTR str, int c, LPSIZE psizl) {
    return hook_GetTextExtentPoint_common(hdc, str, c, psizl, TRUE);
}

static BOOL WINAPI hook_GetTextExtentPointA(HDC hdc, LPCSTR str, int c, LPSIZE psizl) {
    return hook_GetTextExtentPoint_common(hdc, str, c, psizl, FALSE);
}

static int WINAPI hook_DrawTextA(HDC hdc, LPCSTR str, int c, LPRECT lprc, UINT fmt) {
    if (g_utf8_doc_active && str && c != 0 && has_non_ascii(str, c)) {
        int wlen = 0;
        WCHAR *wbuf = utf8_to_utf16_alloc(str, c, &wlen);
        if (wbuf) {
            int ret = DrawTextW(hdc, wbuf, (c < 0) ? -1 : wlen, lprc, fmt);
            tpad_heap_free(wbuf);
            return ret;
        }
    }
    return orig_DrawTextA(hdc, str, c, lprc, fmt);
}

void install_text_hooks(HMODULE exe) {
    iat_hook_module(exe, "kernel32.dll", "MultiByteToWideChar", hook_MultiByteToWideChar, (void **)&g_orig_MB2WC);
    iat_hook_module(exe, "kernel32.dll", "WideCharToMultiByte", hook_WideCharToMultiByte, (void **)&g_orig_WC2MB);
    iat_hook_module(exe, "gdi32.dll", "ExtTextOutA", hook_ExtTextOutA, (void **)&orig_ExtTextOutA);
    iat_hook_module(exe, "gdi32.dll", "GetTextExtentPoint32A", hook_GetTextExtentPoint32A, (void **)&orig_GetTextExtentPoint32A);
    iat_hook_module(exe, "gdi32.dll", "GetTextExtentPointA", hook_GetTextExtentPointA, (void **)&orig_GetTextExtentPointA);
    iat_hook_module(exe, "user32.dll", "DrawTextA", hook_DrawTextA, (void **)&orig_DrawTextA);
    iat_hook_module(exe, "user32.dll", "SetCaretPos", hook_SetCaretPos, (void **)&orig_SetCaretPos);
}
