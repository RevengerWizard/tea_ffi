/*
** C declaration parser
** tea_cparse.h
*/

#ifndef _TEA_CPARSE_H
#define _TEA_CPARSE_H

#include <tea.h>

#include "tea_ctype.h"

CType* cparse_single(tea_State* T, bool* va, bool keep);
void cparse_decl(tea_State* T, const char* p, size_t len);

#endif