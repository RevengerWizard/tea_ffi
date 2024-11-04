/*
** FFI library
** tea_ffi.h
*/

#ifndef _TEA_FFI_H
#define _TEA_FFI_H

#include <stdbool.h>

#include <ffi.h>

#include <tea.h>

#define MAX_RECORD_FIELDS   30
#define MAX_FUNC_ARGS       30

#define CDATA_MT    "cdata"
#define CTYPE_MT    "ctype"
#define CLIB_MT     "clib"

extern const char* crecord_registry;
extern const char* carray_registry;
extern const char* cfunc_registry;
extern const char* ctype_registry;
extern const char* ctdef_registry;
extern const char* clib_registry;

ffi_type* ffi_get_type(size_t size, bool s);
void ffi_tea_num(tea_State* T, ffi_type* ft, void* ptr, int idx);

#endif