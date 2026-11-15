#ifndef TPAD_UTF8_HOOKS_H
#define TPAD_UTF8_HOOKS_H

#include <windows.h>

void install_text_hooks(HMODULE exe);
void cleanup_text_hooks(void);
void install_input_hooks(HMODULE exe);

#endif
