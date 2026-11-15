#ifndef TPAD_UTF8_UTIL_H
#define TPAD_UTF8_UTIL_H

#include <windows.h>

typedef int (WINAPI *PFN_MultiByteToWideChar)(UINT, DWORD, LPCCH, int, LPWSTR, int);
typedef int (WINAPI *PFN_WideCharToMultiByte)(UINT, DWORD, LPCWCH, int, LPSTR, int, LPCCH, LPBOOL);

extern PFN_MultiByteToWideChar g_orig_MB2WC;
extern PFN_WideCharToMultiByte g_orig_WC2MB;
extern BOOL g_utf8_doc_active;

void tpad_log_init(HMODULE hSelf);
void tpad_log_close(void);
void tpad_log(const char *msg);

void mem_copy(void *dst, const void *src, size_t n);
BOOL has_non_ascii(const char *s, int len);
BOOL is_valid_utf8(const char *s, int len);
BOOL has_non_sjis_unicode(const WCHAR *ws, int wlen);

WCHAR *utf8_to_utf16_alloc(const char *s, int len, int *out_wlen);
char *utf16_to_utf8_alloc(const WCHAR *ws, int wlen, int *out_len);
void tpad_heap_free(void *ptr);

#endif
