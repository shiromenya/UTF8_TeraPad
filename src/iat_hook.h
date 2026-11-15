#ifndef TPAD_UTF8_IAT_HOOK_H
#define TPAD_UTF8_IAT_HOOK_H

#include <windows.h>

BOOL iat_hook_module(HMODULE target_module, const char *dll_name, const char *func_name, void *new_func, void **old_func_out);

#endif
