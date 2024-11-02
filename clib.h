/*
** clib.h
*/

#include <tea.h>

typedef struct clib
{
    void* handle;
} clib;

#define CLIB_CACHE 0

void* clib_index(tea_State* T, clib* cl, const char* name);
clib* clib_load(tea_State* T, const char* name, bool global);
void clib_unload(clib* cl);
void clib_default(tea_State* T);
void clib_tostring(tea_State* T, clib* cl);