/*
** C data conversion
** tea_cconv.c
*/

#include <string.h>

#include <ffi.h>

#include "tea_ffi.h"
#include "tea_cdata.h"
#include "tea_cconv.h"

#define PUSH_INTEGER(T, type, ptr) \
    do { \
        type v; \
        memcpy(&v, ptr, sizeof(v)); \
        tea_push_integer(T, v); \
    } while(0)

#define PUSH_NUMBER(T, type, ptr) \
    do { \
        type v; \
        memcpy(&v, ptr, sizeof(v)); \
        tea_push_number(T, v); \
    } while(0)

/* Convert CData to Teascript value */
void cconv_tea_cdata(tea_State* T, CType* ct, void* ptr)
{
    switch(ct->type) 
    {
    case CTYPE_RECORD:
    case CTYPE_ARRAY:
    case CTYPE_PTR:
        cdata_new(T, ct, ptr);
        return;
    }

    switch(ct->ft->type) 
    {
    case FFI_TYPE_SINT8:
        PUSH_INTEGER(T, int8_t, ptr);
        break;
    case FFI_TYPE_UINT8:
        PUSH_INTEGER(T, uint8_t, ptr);
        break;
    case FFI_TYPE_SINT16:
        PUSH_INTEGER(T, int16_t, ptr);
        break;
    case FFI_TYPE_UINT16:
        PUSH_INTEGER(T, uint16_t, ptr);
        break;
    case FFI_TYPE_SINT32:
        PUSH_INTEGER(T, int32_t, ptr);
        break;
    case FFI_TYPE_UINT32:
        PUSH_INTEGER(T, uint32_t, ptr);
        break;
    case FFI_TYPE_SINT64:
        PUSH_INTEGER(T, int64_t, ptr);
        break;
    case FFI_TYPE_UINT64:
        PUSH_INTEGER(T, uint64_t, ptr);
        break;
    case FFI_TYPE_FLOAT:
        PUSH_NUMBER(T, float, ptr);
        break;
    case FFI_TYPE_DOUBLE:
        PUSH_NUMBER(T, double, ptr);
        break;
    default:
        break;
    }
}

/* Convert Teascript number to CData */
static bool cconv_tea_num(tea_State* T, CType* ct, void* ptr, int idx, bool cast)
{
    if(ct->type == CTYPE_PTR)
    {
        if(!cast)
            return false;
        *(void**)ptr = (void*)(intptr_t)tea_to_integer(T, idx);
        return true;
    }

    if(!ctype_is_num(ct))
        return false;

    ffi_tea_num(T, ct->ft, ptr, idx);

    if(ct->type == CTYPE_BOOL)
        *(int8_t*)ptr = !!*(int8_t*)ptr;

    return true;
}

static bool cconv_cdata_ptr(tea_State* T, CType* ct, void* ptr,
                CType* from_ct, void* from_ptr, bool cast)
{
    if(ct->type == CTYPE_PTR && (cast || ctype_equal(ct->ptr, from_ct)
            || ctype_ptr_to(ct, CTYPE_VOID) || from_ct->type == CTYPE_VOID))
    {
        *(void**)ptr = from_ptr;
        return true;
    }

    if(cast && ctype_is_int(ct))
    {
        tea_push_integer(T, (intptr_t)from_ptr);
        cconv_tea_num(T, ct, ptr, -1, true);
        tea_pop(T, 1);
        return true;
    }

    return false;
}

/* Convert a Teascript userdata CData to a CData */
static bool cconv_udata_cdata(tea_State* T, CType* ct, void* ptr, int idx, bool cast)
{
    CData* cd = (CData*)tea_to_userdata(T, idx);

    switch(cdata_type(cd))
    {
    case CTYPE_ARRAY:
        return cconv_cdata_ptr(T, ct, ptr, cd->ct->array->ct, cdata_ptr(cd), cast);
    case CTYPE_PTR:
        return cconv_cdata_ptr(T, ct, ptr, cd->ct->ptr, cdata_ptr_ptr(cd), cast);
    case CTYPE_RECORD:
        if(ct->type == CTYPE_PTR && (cast || ctype_equal(cd->ct, ct->ptr)))
        {
            *(void**)ptr = cdata_ptr(cd);
            return true;
        }

        if(ctype_equal(cd->ct, ct))
        {
            memcpy(ptr, cdata_ptr(cd), ctype_sizeof(ct));
            return true;
        }
        break;
    default:
        if(ctype_is_num(cd->ct))
        {
            cconv_tea_cdata(T, cd->ct, cdata_ptr(cd));
            cconv_tea_num(T, ct, ptr, -1, cast);
            tea_pop(T, 1);
            return true;
        }
        break;
    }

    return false;
}

/* Convert Teascript list to CData */
static void cconv_tea_list(tea_State* T, CType* ct, void* ptr, int idx, bool cast)
{
    int i = 0;
    while(i < ct->array->size)
    {
        if(!tea_get_item(T, idx, i))
        {
            break;
        }
        cconv_cdata_tea(T, ct->array->ct, ((char*)(ptr)) + ctype_sizeof(ct->array->ct) * i++, -1, cast);
        tea_pop(T, 1);
    }
}

/* Convert Teascript map to CData */
static void cconv_tea_map(tea_State* T, CType* ct, void* ptr, int idx, bool cast)
{
    int i = 0;
    while(i < ct->rc->nfield)
    {
        CRecordField* field = ct->rc->fields[i++];

        if(tea_get_key(T, idx, field->name))
        {
            cconv_cdata_tea(T, field->ct, ((char*)(ptr)) + field->offset, -1, cast);
            tea_pop(T, 1);
        }
    }
}

/* Convert Teascript value to CData */
void cconv_cdata_tea(tea_State* T, CType* ct, void* ptr, int idx, bool cast)
{
    switch(ct->type)
    {
    case CTYPE_FUNC:
    case CTYPE_VOID:
        tea_error(T, "invalid C type");
        break;
    case CTYPE_ARRAY:
    case CTYPE_RECORD:
        if(cast)
            tea_error(T, "invalid C type");
        break;
    default:
        break;
    }

    switch(tea_get_type(T, idx))
    {
    case TEA_TYPE_NIL:
        if(ct->type == CTYPE_PTR)
        {
            *(void**)ptr = NULL;
            return;
        }
        break;
    case TEA_TYPE_NUMBER:
    case TEA_TYPE_BOOL:
        if(cconv_tea_num(T, ct, ptr, idx, cast))
            return;
        break;
    case TEA_TYPE_STRING:
        if(cast || ((ctype_ptr_to(ct, CTYPE_CHAR) || ctype_ptr_to(ct, CTYPE_VOID))
            && ct->ptr->is_const))
        {
            *(const char**)ptr = (const char*)tea_get_string(T, idx);
            return;
        }

        if(ct->type == CTYPE_ARRAY && ct->array->ct->type == CTYPE_CHAR)
        {
            size_t len;
            const char* str = tea_get_lstring(T, idx, &len);
            memcpy(ptr, str, len + 1);
            return;
        }
        break;
    case TEA_TYPE_USERDATA:
        if(tea_test_udata(T, idx, CDATA_MT))
        {
            if(cconv_udata_cdata(T, ct, ptr, idx, cast))
                return;
        }
        else if(ct->type == CTYPE_PTR)
        {
            void* ud = tea_to_userdata(T, idx);

            if(cast || ctype_ptr_to(ct, CTYPE_VOID))
            {
                *(void**)ptr = ud;
                return;
            }
        }
        break;
    case TEA_TYPE_POINTER:
        if(ct->type == CTYPE_PTR)
        {
            *(void**)ptr = (void*)tea_to_pointer(T, idx);
            return;
        }
        break;
    case TEA_TYPE_LIST:
        if(ct->type == CTYPE_ARRAY)
        {
            cconv_tea_list(T, ct, ptr, idx, cast);
            return;
        }
        break;
    case TEA_TYPE_MAP:
        if(ct->type == CTYPE_RECORD)
        {
            cconv_tea_map(T, ct, ptr, idx, cast);
            return;
        }
        break;
    default:
        break;
    }

    if(tea_test_udata(T, idx, CDATA_MT))
    {
        CData* cd = tea_to_userdata(T, idx);
        ctype_tostring(T, cd->ct);
        ctype_tostring(T, ct);
        tea_push_fstring(T, "cannot convert '%s' to '%s'", tea_get_string(T, -2), tea_get_string(T, -1));
    }
    else
    {
        ctype_tostring(T, ct);
        tea_push_fstring(T, "cannot convert '%s' to '%s'", tea_typeof(T, idx), tea_get_string(T, -1));
    }
    tea_arg_error(T, idx, tea_get_string(T, -1));
}