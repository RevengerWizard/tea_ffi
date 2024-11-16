/*
** C data management
** tea_cdata.c
*/

#include <string.h>

#include "tea_ffi.h"
#include "teax.h"
#include "cdata.h"

CData* cdata_new(tea_State* T, CType* ct, void* ptr)
{
    CData* cd = tea_new_udatav(T, sizeof(CData) + (ptr ? 0 : ctype_sizeof(ct)), 1, CDATA_MT);
    cd->ptr = ptr;
    cd->ct = ct;

    tea_new_map(T);
    tea_set_fieldp(T, TEA_REGISTRY_INDEX, cd);

    if(!ptr)
        memset(cdata_ptr(cd), 0, ctype_sizeof(ct));

    return cd;
}

void* cdata_ptr_ptr(CData* cd)
{
    int type = cdata_type(cd);

    if(type != CTYPE_PTR && type != CTYPE_FUNC)
        return NULL;

    return *(void**)cdata_ptr(cd);
}

void cdata_ptr_set(CData* cd, void* ptr)
{
    int type = cdata_type(cd);

    if(type != CTYPE_PTR && type != CTYPE_FUNC)
        return;

    *(void**)cdata_ptr(cd) = ptr;
}