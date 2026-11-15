#include "iat_hook.h"

static int ascii_stricmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int ascii_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

BOOL iat_hook_module(HMODULE target_module, const char *dll_name, const char *func_name, void *new_func, void **old_func_out) {
    if (!target_module) return FALSE;
    BYTE *base = (BYTE *)target_module;
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;

    PIMAGE_NT_HEADERS32 nt = (PIMAGE_NT_HEADERS32)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return FALSE;

    IMAGE_DATA_DIRECTORY imp_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imp_dir.VirtualAddress == 0 || imp_dir.Size == 0) return FALSE;

    BOOL hooked = FALSE;
    PIMAGE_IMPORT_DESCRIPTOR desc = (PIMAGE_IMPORT_DESCRIPTOR)(base + imp_dir.VirtualAddress);
    for (; desc->Name != 0; desc++) {
        const char *mod_name = (const char *)(base + desc->Name);
        if (ascii_stricmp(mod_name, dll_name) != 0) continue;

        PIMAGE_THUNK_DATA32 orig_thunk = (PIMAGE_THUNK_DATA32)(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        PIMAGE_THUNK_DATA32 iat_thunk = (PIMAGE_THUNK_DATA32)(base + desc->FirstThunk);

        for (; iat_thunk->u1.Function != 0; orig_thunk++, iat_thunk++) {
            if (IMAGE_SNAP_BY_ORDINAL32(orig_thunk->u1.Ordinal)) continue;
            PIMAGE_IMPORT_BY_NAME imp_name = (PIMAGE_IMPORT_BY_NAME)(base + orig_thunk->u1.AddressOfData);
            if (ascii_strcmp((const char *)imp_name->Name, func_name) != 0) continue;

            DWORD old_prot = 0;
            if (VirtualProtect(&iat_thunk->u1.Function, sizeof(DWORD), PAGE_READWRITE, &old_prot)) {
                if (old_func_out && !*old_func_out) {
                    *old_func_out = (void *)(DWORD_PTR)iat_thunk->u1.Function;
                }
                iat_thunk->u1.Function = (DWORD)(DWORD_PTR)new_func;
                VirtualProtect(&iat_thunk->u1.Function, sizeof(DWORD), old_prot, &old_prot);
                hooked = TRUE;
            }
        }
    }
    return hooked;
}
