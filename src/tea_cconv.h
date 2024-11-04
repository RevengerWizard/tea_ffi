/*
** C data conversion
** tea_cconv.h
*/

#ifndef _TEA_CCONV_H
#define _TEA_CCONV_H

#include <tea.h>

#include "tea_ctype.h"

void cconv_tea_cdata(tea_State* T, CType* ct, void* ptr);
void cconv_cdata_tea(tea_State* T, CType* ct, void* ptr, int idx, bool cast);

#endif