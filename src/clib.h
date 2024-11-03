/*
** clib.h
*/

#include <tea.h>

typedef struct CLibrary
{
    void* handle;
} CLibrary;

#define CLIB_CACHE 0

void* clib_index(tea_State* T, CLibrary* cl, const char* name);
CLibrary* clib_load(tea_State* T, const char* name, bool global);
void clib_unload(CLibrary* cl);
void clib_default(tea_State* T);
void clib_tostring(tea_State* T, CLibrary* cl);