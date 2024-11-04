/*
** C type management
** tea_ctype.c
*/

#include <stdio.h>

#include "teax.h"
#include "tea_ffi.h"
#include "tea_ctype.h"

CArray* carray_lookup(tea_State* T, size_t size, CType* ct)
{
    CArray* a;

    tea_get_fieldp(T, TEA_REGISTRY_INDEX, &carray_registry);

    tea_push_nil(T);
    while(tea_next(T, -2) != 0)
    {
        a = (CArray*)tea_to_userdata(T, -1);
        if(a->size == size && ctype_equal(a->ct, ct))
        {
            tea_pop(T, 3);
            return a;
        }
        tea_pop(T, 1);
    }

    a = tea_new_userdata(T, sizeof(CArray));
    if(!a)
        tea_error(T, "no mem");

    tea_set_fieldp(T, -2, a);
    tea_pop(T, 1);

    if(size)
    {
        a->ft.type = FFI_TYPE_STRUCT;
        a->ft.alignment = ctype_ft(ct)->alignment;
        a->ft.size = ctype_sizeof(ct) * size;
    }

    a->size = size;
    a->ct = ctype_lookup(T, ct, false);

    return a;
}

static CType* ctype_new(tea_State* T, bool keep)
{
    CType* ct = tea_new_udata(T, sizeof(CType), CTYPE_MT);
    ct->type = CTYPE_VOID;
    ct->is_const = false;

    tea_get_fieldp(T, TEA_REGISTRY_INDEX, &ctype_registry);
    tea_push_value(T, -2);
    tea_set_fieldp(T, -2, ct);

    if(keep)
        tea_pop(T, 1);
    else
        tea_pop(T, 2);

    return ct;
}

CType* ctype_lookup(tea_State* T, CType* match, bool keep)
{
    CType* ct;

    tea_get_fieldp(T, TEA_REGISTRY_INDEX, &ctype_registry);

    tea_push_nil(T);
    while(tea_next(T, -2) != 0)
    {
        ct = tea_to_userdata(T, -1);
        if(ctype_equal(match, ct))
        {
            if(keep)
            {
                tea_replace(T, -3);
                tea_pop(T, 1);
            }
            else
            {
                tea_pop(T, 3);
            }
            return ct;
        }
        tea_pop(T, 1);
    }

    tea_pop(T, 1);

    ct = ctype_new(T, keep);
    *ct = *match;

    return ct;
}

bool ctype_equal(const CType* ct1, const CType* ct2)
{
    if(ct1->type != ct2->type)
        return false;

    if(ct1->is_const != ct2->is_const)
        return false;

    switch(ct1->type)
    {
    case CTYPE_RECORD:
        return ct1->rc == ct2->rc;
    case CTYPE_ARRAY:
        if(ct1->array->size != ct2->array->size)
            return false;
        return ctype_equal(ct1->array->ct, ct2->array->ct);
    case CTYPE_PTR:
        return ctype_equal(ct1->ptr, ct2->ptr);
    case CTYPE_FUNC:
        return false;
    default:
        break;
    }

    return true;
}

const char* ctype_name(CType* ct)
{
    switch(ct->type)
    {
    case CTYPE_BOOL:
        return "bool";
    case CTYPE_CHAR:
        return "char";
    case CTYPE_SHORT:
        return "short";
    case CTYPE_INT:
        return "int";
    case CTYPE_LONG:
        return "long";
    case CTYPE_LONGLONG:
        return "long long";
    case CTYPE_UCHAR:
        return "unsigned char";
    case CTYPE_USHORT:
        return "unsigned short";
    case CTYPE_UINT:
        return "unsigned int";
    case CTYPE_ULONG:
        return "unsigned long";
    case CTYPE_ULONGLONG:
        return "unsigned long long";
    case CTYPE_FLOAT:
        return "float";
    case CTYPE_DOUBLE:
        return "double";
    case CTYPE_INT8_T:
        return "int8_t";
    case CTYPE_INT16_T:
        return "int16_t";
    case CTYPE_INT32_T:
        return "int32_t";
    case CTYPE_INT64_T:
        return "int64_t";
    case CTYPE_UINT8_T:
        return "uint8_t";
    case CTYPE_UINT16_T:
        return "uint16_t";
    case CTYPE_UINT32_T:
        return "uint32_t";
    case CTYPE_UINT64_T:
        return "uint64_t";
    case CTYPE_SIZE_T:
        return "size_t";
    case CTYPE_VOID:
        return "void";
    case CTYPE_RECORD:
        return ct->rc->is_union ? "union" : "struct";
    case CTYPE_ARRAY:
        return "array";
    case CTYPE_PTR:
        return "pointer";
    case CTYPE_FUNC:
        return "func";
    default:
        return "unknown";
    }
}

static const char* cstruct_lookup_name(tea_State* T, CRecord* st)
{
    tea_get_fieldp(T, TEA_REGISTRY_INDEX, &crecord_registry);

    tea_push_nil(T);
    while(tea_next(T, -2) != 0)
    {
        if((CRecord*)tea_to_pointer(T, -1) == st)
        {
            tea_pop(T, 1);
            tea_remove(T, -2);
            return tea_to_string(T, -1);
        }
        tea_pop(T, 1);
    }
    return NULL;
}

void __ctype_tostring(tea_State* T, CType* ct, teaB_buffer* b)
{
    char buf[128];
    int i;

    if(ct->type != CTYPE_PTR && ct->is_const)
        teaB_addstring(b, "const ");

    switch(ct->type)
    {
    case CTYPE_PTR:
        __ctype_tostring(T, ct->ptr, b);
        teaB_addchar(b, '*');
        if(ct->is_const)
            teaB_addstring(b, " const");
        break;
    case CTYPE_ARRAY:
        __ctype_tostring(T, ct->array->ct, b);
        snprintf(buf, sizeof(buf), "%llu", ct->array->size);
        teaB_addchar(b, '[');
        teaB_addstring(b, buf);
        teaB_addchar(b, ']');
        break;
    case CTYPE_FUNC:
        __ctype_tostring(T, ct->func->rtype, b);
        teaB_addstring(b, " (");
        for(i = 0; i < ct->func->narg; i++)
        {
            if(i > 0)
                teaB_addchar(b, ',');
            __ctype_tostring(T, ct->func->args[i], b);
        }
        teaB_addchar(b, ')');
        break;
    default:
        teaB_addstring(b, ctype_name(ct));
        if(ct->type == CTYPE_RECORD && !ct->rc->anonymous)
        {
            teaB_addchar(b, ' ');
            teaB_addstring(b, cstruct_lookup_name(T, ct->rc));
            tea_pop(T, 2);
        }
        break;
    }
}

void ctype_tostring(tea_State* T, CType* ct)
{
    teaB_buffer b;
    teaB_buffinit(T, &b);
    __ctype_tostring(T, ct, &b);
    teaB_pushresult(&b);
}