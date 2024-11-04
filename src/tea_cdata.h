/*
** C data management
** tea_cdata.h
*/

#ifndef _TEA_CDATA_H
#define _TEA_CDATA_H

#include <tea.h>

#include "tea_ctype.h"

CData* cdata_new(tea_State* T, CType* ct, void* ptr);
void* cdata_ptr_ptr(CData* cd);
void cdata_ptr_set(CData* cd, void* ptr);

static inline int cdata_type(CData* cd)
{
    return cd->ct->type;
}

static inline void* cdata_ptr(CData* cd)
{
    return cd->ptr ? cd->ptr : cd + 1;
}

#endif