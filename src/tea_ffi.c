/*
** FFI library
** tea_ffi.c
*/

#include <string.h>

#include <ffi.h>

#define TEA_LIB
#include <tea.h>

#include "teax.h"
#include "alloca_compat.h"

#include "arch.h"
#include "tea_ffi.h"
#include "tea_clib.h"
#include "tea_ctype.h"
#include "tea_cdata.h"
#include "tea_cparse.h"

const char* crecord_registry;
const char* carray_registry;
const char* cfunc_registry;
const char* ctype_registry;
const char* ctdef_registry;
const char* clib_registry;

ffi_type* ffi_get_type(size_t size, bool s)
{
    switch(size)
    {
    case 8:
        return s ? &ffi_type_sint64 : &ffi_type_uint64;
    case 4:
        return s ? &ffi_type_sint32 : &ffi_type_uint32;
    case 2:
        return s ? &ffi_type_sint16 : &ffi_type_uint16;
    default:
        return s ? &ffi_type_sint8 : &ffi_type_uint8;
    }
}

static void __cdata_tostring(tea_State* T, CData* cd)
{
    void* ptr = (cdata_type(cd) == CTYPE_PTR) ? cdata_ptr_ptr(cd) : cdata_ptr(cd);
    
    teaB_buffer b;
    teaB_buffinit(T, &b);
    teaB_addstring(&b, "cdata<");
    __ctype_tostring(T, cd->ct, &b);
    const char* s = tea_push_fstring(T, ">: %p", ptr);
    teaB_addstring(&b, s);
    teaB_pushresult(&b);
}

static void ffi_cdata_tostring(tea_State* T)
{
    CData* cd = tea_check_udata(T, 0, CDATA_MT);
    __cdata_tostring(T, cd);
}

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

static void cdata_to_tea(tea_State* T, CType* ct, void* ptr)
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

static void cdata_from_tea(tea_State* T, CType* ct, void* ptr, int idx, bool cast);

static tea_Integer from_tea_num_int(tea_State* T, int idx)
{
    if(tea_is_bool(T, idx))
        return tea_get_bool(T, idx);
    else
        return tea_check_integer(T, idx);
}

static tea_Number from_tea_num_num(tea_State* T, int idx)
{
    if(tea_is_bool(T, idx))
        return tea_get_bool(T, idx);
    else
        return tea_check_number(T, idx);
}

static void ft_from_tea_num(tea_State* T, ffi_type* ft, void* ptr, int idx)
{
    switch(ft->type) {
    case FFI_TYPE_SINT8:
        *(int8_t*)ptr = from_tea_num_int(T, idx);
        break;
    case FFI_TYPE_UINT8:
        *(uint8_t*)ptr = from_tea_num_int(T, idx);
        break;
    case FFI_TYPE_SINT16:
        *(int16_t*)ptr = from_tea_num_int(T, idx);
        break;
    case FFI_TYPE_UINT16:
        *(uint16_t*)ptr = from_tea_num_int(T, idx);
        break;
    case FFI_TYPE_SINT32:
        *(int32_t*)ptr = from_tea_num_int(T, idx);
        break;
    case FFI_TYPE_UINT32:
        *(uint32_t*)ptr = from_tea_num_int(T, idx);
        break;
    case FFI_TYPE_SINT64:
        *(int64_t*)ptr = from_tea_num_int(T, idx);
        break;
    case FFI_TYPE_UINT64:
        *(uint64_t*)ptr = from_tea_num_int(T, idx);
        break;
    case FFI_TYPE_FLOAT:
        *(float*)ptr = from_tea_num_num(T, idx);
        break;
    case FFI_TYPE_DOUBLE:
        *(double*)ptr = from_tea_num_num(T, idx);
        break;
    }
}

static bool cdata_from_tea_num(tea_State* T, CType* ct, void* ptr, int idx, bool cast)
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

    ft_from_tea_num(T, ct->ft, ptr, idx);

    if(ct->type == CTYPE_BOOL)
        *(int8_t*)ptr = !!*(int8_t*)ptr;

    return true;
}

static bool cdata_from_tea_cdata_ptr(tea_State* T, CType* ct, void* ptr,
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
        cdata_from_tea_num(T, ct, ptr, -1, true);
        tea_pop(T, 1);
        return true;
    }

    return false;
}

static bool cdata_from_tea_cdata(tea_State* T, CType* ct, void* ptr, int idx, bool cast)
{
    CData* cd = (CData*)tea_to_userdata(T, idx);

    switch(cdata_type(cd))
    {
    case CTYPE_ARRAY:
        return cdata_from_tea_cdata_ptr(T, ct, ptr, cd->ct->array->ct, cdata_ptr(cd), cast);
    case CTYPE_PTR:
        return cdata_from_tea_cdata_ptr(T, ct, ptr, cd->ct->ptr, cdata_ptr_ptr(cd), cast);
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
            cdata_to_tea(T, cd->ct, cdata_ptr(cd));
            cdata_from_tea_num(T, ct, ptr, -1, cast);
            tea_pop(T, 1);
            return true;
        }
        break;
    }

    return false;
}

static void cdata_from_tea_list(tea_State* T, CType* ct, void* ptr, int idx, bool cast)
{
    int i = 0;

    while(i < ct->array->size)
    {
        if(!tea_get_item(T, idx, i))
        {
            break;
        }
        cdata_from_tea(T, ct->array->ct, ((char*)(ptr)) + ctype_sizeof(ct->array->ct) * i++, -1, cast);
        tea_pop(T, 1);
    }
}

static void cdata_from_tea_map(tea_State* T, CType* ct, void* ptr, int idx, bool cast)
{
    int i = 0;
    while(i < ct->rc->nfield)
    {
        CRecordField* field = ct->rc->fields[i++];

        if(tea_get_key(T, idx, field->name))
        {
            cdata_from_tea(T, field->ct, ((char*)(ptr)) + field->offset, -1, cast);
            tea_pop(T, 1);
        }
    }
}

static void cdata_from_tea(tea_State* T, CType* ct, void* ptr, int idx, bool cast)
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
        if(cdata_from_tea_num(T, ct, ptr, idx, cast))
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
            if(cdata_from_tea_cdata(T, ct, ptr, idx, cast))
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
            cdata_from_tea_list(T, ct, ptr, idx, cast);
            return;
        }
        break;
    case TEA_TYPE_MAP:
        if(ct->type == CTYPE_RECORD)
        {
            cdata_from_tea_map(T, ct, ptr, idx, cast);
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

static void cdata_index_ptr(tea_State* T, CData* cd, CType* ct, bool to)
{
    void* ptr = (cdata_type(cd) == CTYPE_PTR) ? cdata_ptr_ptr(cd) : cdata_ptr(cd);
    int idx;

    if(ct->type == CTYPE_VOID)
    {
        ctype_tostring(T, cd->ct);
        tea_error(T, "ctype '%s' cannot be indexed", tea_get_string(T, -1));
    }

    if(!tea_is_number(T, 1))
    {
        ctype_tostring(T, cd->ct);
        tea_error(T, "ctype '%s' cannot be indexed with %s", tea_get_string(T, -1), tea_typeof(T, 2));
    }

    idx = tea_to_integer(T, 1);

    if(to)
    {
        tea_get_fieldp(T, TEA_REGISTRY_INDEX, cd);
        if(tea_get_fieldi(T, -1, idx))
        {
            tea_remove(T, -2);
            return;
        }
        tea_pop(T, 1);

        cdata_to_tea(T, ct, ((char*)ptr) + ctype_sizeof(ct) * idx);

        if(tea_test_udata(T, -1, CDATA_MT))
        {
            tea_get_fieldp(T, TEA_REGISTRY_INDEX, cd);
            tea_push_value(T, -2);
            tea_set_fieldi(T, -2, idx);
            tea_pop(T, 1);
        }
        return;
    }
    else
    {
        cdata_from_tea(T, ct, ((char*)ptr) + ctype_sizeof(ct) * idx, 2, false);
    }
}

static void cdata_index_common(tea_State* T, bool to)
{
    CData* cd = tea_check_udata(T, 0, CDATA_MT);
    CType* ct = cd->ct;

    if(!to && ct->is_const)
        tea_error(T, "assignment of read-only variable");

    switch(ct->type)
    {
    case CTYPE_PTR:
        cdata_index_ptr(T, cd, ct->ptr, to);
        return;
    case CTYPE_ARRAY:
        cdata_index_ptr(T, cd, ct->array->ct, to);
        return;
    default:
        break;
    }
    ctype_tostring(T, cd->ct);
    tea_error(T, "ctype '%s' cannot be indexed", tea_get_string(T, -1));
}

static void ffi_cdata_getindex(tea_State* T)
{
    cdata_index_common(T, true);
}

static void ffi_cdata_setindex(tea_State* T)
{
    cdata_index_common(T, false);
}

static CRecordField* cdata_crecord_find_field(
        CRecordField** fields, int nfield, const char* name, size_t* offset)
{
    int i;
    for(i = 0; i < nfield; i++)
    {
        CRecordField* field = fields[i];

        if(field->name[0])
        {
            if(!strcmp(field->name, name))
            {
                *offset += field->offset;
                return field;
            }
        }
        else
        {
            field = cdata_crecord_find_field(field->ct->rc->fields, field->ct->rc->nfield, name, offset);
            if(field)
            {
                *offset += fields[i]->offset;
                return field;
            }
        }
    }
    return NULL;
}

static void cdata_index_crecord(tea_State* T, CData* cd, CType* ct, bool to)
{
    void* ptr = (cdata_type(cd) == CTYPE_PTR) ? cdata_ptr_ptr(cd) : cdata_ptr(cd);
    CRecord* rc = ct->rc;
    CRecordField* field;
    size_t offset = 0;
    const char* name;

    name = tea_check_string(T, 1);

    if(to)
    {
        tea_get_fieldp(T, TEA_REGISTRY_INDEX, cd);
        if(tea_get_key(T, -1, name))
        {
            tea_remove(T, -2);
            return;
        }
        tea_pop(T, 1);
    }

    field = cdata_crecord_find_field(rc->fields, rc->nfield, name, &offset);
    if(!field)
    {
        ctype_tostring(T, ct);
        tea_error(T, "ctype '%s' has no member named '%s'", tea_get_string(T, -1), name);
        return;
    }

    if(to)
    {
        cdata_to_tea(T, field->ct, ((char*)(ptr)) + offset);
        if(tea_test_udata(T, -1, CDATA_MT))
        {
            tea_get_fieldp(T, TEA_REGISTRY_INDEX, cd);
            tea_push_value(T, -2);
            tea_set_key(T, -2, name);
            tea_pop(T, 1);
        }
        return;
    }
    else
    {
        cdata_from_tea(T, field->ct, ((char*)(ptr)) + offset, 2, false);
    }
}

static void cdata_attr_common(tea_State* T, bool to)
{
    CData* cd = tea_check_udata(T, 0, CDATA_MT);
    CType* ct = cd->ct;

    if(!to && ct->is_const)
        tea_error(T, "assignment of read-only variable");

    switch(ct->type)
    {
    case CTYPE_RECORD:
        return cdata_index_crecord(T, cd, ct, to);
    case CTYPE_PTR:
        if(ctype_ptr_to(ct, CTYPE_RECORD))
            return cdata_index_crecord(T, cd, ct->ptr, to);
    default:
        break;
    }
    ctype_tostring(T, cd->ct);
    tea_error(T, "cannot get attribute of CType '%s'", tea_get_string(T, -1));
}

static void ffi_cdata_getattr(tea_State* T)
{
    cdata_attr_common(T, true);
}

static void ffi_cdata_setattr(tea_State* T)
{
    cdata_attr_common(T, false);
}

static void ffi_cdata_eq(tea_State* T)
{
    if(!tea_is_userdata(T, 0) && tea_is_userdata(T, 1))
    {
        tea_swap(T, 0, 1);
    }

    CData* cd = tea_check_udata(T, 0, CDATA_MT);
    int type = cdata_type(cd);
    CData* a;
    bool eq = false;

    switch(type)
    {
    case CTYPE_RECORD:
    case CTYPE_ARRAY:
    case CTYPE_FUNC:
        break;
    case CTYPE_PTR:
        if(tea_is_nil(T, 1))
        {
            eq = cdata_ptr_ptr(cd) == NULL;
            break;
        }

        a = tea_test_udata(T, 1, CDATA_MT);
        if(a && cdata_type(a) == CTYPE_PTR)
            eq = cdata_ptr_ptr(cd) == cdata_ptr_ptr(a);

        break;
    default:
        cdata_to_tea(T, cd->ct, cdata_ptr(cd));
        eq = tea_equal(T, 1, -1);
        tea_pop(T, 1);
    }

    tea_push_bool(T, eq);
}

static ffi_type* to_vararg(tea_State* T, int idx)
{
    CData* cd;

    switch(tea_get_type(T, idx))
    {
    case TEA_TYPE_BOOL:
        return &ffi_type_sint; /* cannot be less than the size of int, due to limited in libffi */
    case TEA_TYPE_NUMBER:
        if(tea_is_integer(T, idx))
            return ffi_get_type(sizeof(tea_Integer), false);
        return &ffi_type_double;
    case TEA_TYPE_NIL:
    case TEA_TYPE_STRING:
    case TEA_TYPE_POINTER:
        return &ffi_type_pointer;
    case TEA_TYPE_USERDATA:
        cd = tea_test_udata(T, idx, CDATA_MT);
        if(!cd || cdata_type(cd) == CTYPE_RECORD || cdata_type(cd) == CTYPE_ARRAY)
            return &ffi_type_pointer;
        return ctype_ft(cd->ct);
    default:
        return NULL;
    }
}

static void ffi_cdata_call(tea_State* T)
{
    CData* cd = tea_check_udata(T, 0, CDATA_MT);
    ffi_type* args[MAX_FUNC_ARGS] = {0};
    void* values[MAX_FUNC_ARGS] = {0};
    CType* ct = cd->ct;
    int i, status, narg;
    CFunc* func;
    CType* rtype;
    ffi_cif cif;
    void* sym;

    if(ct->type != CTYPE_FUNC)
    {
        ctype_tostring(T, ct);
        tea_error(T, "'%s' is not callable", tea_get_string(T, -1));
        return;
    }

    sym = cdata_ptr_ptr(cd);
    func = ct->func;
    rtype = func->rtype;

    narg = tea_get_top(T) - 1;

    if(func->va)
    {
        if(narg < func->narg)
            tea_error(T, "wrong number of arguments for function call");
    }
    else if(narg != func->narg)
    {
        tea_error(T, "wrong number of arguments for function call");
    }

    for(i = 0; i < func->narg; i++)
    {
        args[i] = ctype_ft(func->args[i]);
        values[i] = alloca(args[i]->size);
        cdata_from_tea(T, func->args[i], values[i], i + 1, false);
    }

    if(func->va)
    {
        for(i = func->narg; i < narg; i++)
        {
            args[i] = to_vararg(T, i + 1);
            if(!args[i])
                tea_error(T, "unsupported type '%s'", tea_typeof(T, i + 1));
            values[i] = alloca(args[i]->size);
        }

        for(i = func->narg; i < narg; i++)
        {
            switch(tea_get_type(T, i + 1))
            {
            case TEA_TYPE_BOOL:
            case TEA_TYPE_NUMBER:
                ft_from_tea_num(T, args[i], values[i], i + 1);
                break;
            case TEA_TYPE_NIL:
                *(void**)values[i] = NULL;
                break;
            case TEA_TYPE_STRING:
                *(void**)values[i] = (void*)tea_check_string(T, i + 1);
                break;
            case TEA_TYPE_POINTER:
                *(void**)values[i] = (void*)tea_to_pointer(T, i + 1);
                break;
            case TEA_TYPE_USERDATA:
                cd = tea_test_udata(T, i + 1, CDATA_MT);
                if(!cd)
                    *(void**)values[i] = tea_to_userdata(T, i + 1);
                else if(cdata_type(cd) == CTYPE_RECORD || cdata_type(cd) == CTYPE_ARRAY)
                    *(void**)values[i] = cdata_ptr(cd);
                else if(cdata_type(cd) == CTYPE_FUNC || cdata_type(cd) == CTYPE_PTR)
                    *(void**)values[i] = cdata_ptr_ptr(cd);
                else
                {
                    cdata_to_tea(T, cd->ct, cdata_ptr(cd));
                    ft_from_tea_num(T, args[i], values[i], -1);
                    tea_pop(T, 1);
                }
                break;
            }
        }
    }

    if(func->va)
        status = ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI, func->narg, narg, ctype_ft(rtype), args);
    else
        status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, func->narg, ctype_ft(rtype), args);
    if(status)
        tea_error(T, "ffi_prep_cif fail: %d", status);

    if(rtype->type == CTYPE_RECORD || rtype->type == CTYPE_PTR)
    {
        if(rtype->type == CTYPE_PTR)
        {
            void* rvalue;
            ffi_call(&cif, FFI_FN(sym), &rvalue, values);
            cdata_ptr_set(cdata_new(T, rtype, NULL), rvalue);
        }
        else
        {
            cd = cdata_new(T, rtype, NULL);
            ffi_call(&cif, FFI_FN(sym), cdata_ptr(cd), values);
        }
        return;
    }

    if(rtype->type <= CTYPE_RECORD)
    {
        void* rvalue = NULL;

        if(rtype->type != CTYPE_VOID)
            rvalue = alloca(ctype_sizeof(rtype));

        ffi_call(&cif, FFI_FN(sym), rvalue, values);

        cdata_to_tea(T, rtype, rvalue);
        return;
    }

    tea_error(T, "unsupported return type '%s'", ctype_name(rtype));
}

static void ffi_cdata_gc(tea_State* T)
{
    CData* cd = tea_check_udata(T, 0, CDATA_MT);

    tea_get_udvalue(T, 0, 0);
    if(!tea_is_nil(T, -1))
    {
        tea_push_value(T, 0);
        tea_pcall(T, 1);
        tea_pop(T, 1);
    }

    tea_push_pointer(T, cd);
    tea_delete_field(T, TEA_REGISTRY_INDEX);

    tea_push_nil(T);
}

static const tea_Methods cdata_methods[] = {
    { "==", "static", ffi_cdata_eq, 2, 0 },
    { "call", "method", ffi_cdata_call, TEA_VARG, 0 },
    { "[]", "method", ffi_cdata_getindex, 2, 0 },
    { "[]=", "method", ffi_cdata_setindex, 3, 0 },
    { "getattr", "method", ffi_cdata_getattr, 2, 0 },
    { "setattr", "method", ffi_cdata_setattr, 3, 0 },
    { "tostring", "method", ffi_cdata_tostring, 1, 0 },
    { "gc", "method", ffi_cdata_gc, 1, 0 },
    { NULL, NULL }
};

static void ffi_ctype_tostring(tea_State* T)
{
    CType* ct = tea_check_udata(T, 0, CTYPE_MT);
    tea_push_literal(T, "ctype<");
    ctype_tostring(T, ct);
    tea_push_literal(T, ">");
    tea_concat(T, 3);
}

static void ffi_ctype_gc(tea_State* T)
{
    CType* ct = tea_check_udata(T, 0, CTYPE_MT);
    int type = ct->type;

    if(type == CTYPE_RECORD && ct->rc->anonymous)
    {
        int i;
        for(i = 0; i < ct->rc->nfield; i++)
            free(ct->rc->fields[i]);

        free(ct->rc);
    }

    tea_push_nil(T);
}

static const tea_Methods ctype_methods[] = {
    { "tostring", "method", ffi_ctype_tostring, 1, 0 },
    { "gc", "method", ffi_ctype_gc, 1, 0 },
    { NULL, NULL }
};

static void ffi_clib_getattr(tea_State* T)
{
    CLibrary* cl = tea_check_udata(T, 0, CLIB_MT);
    const char* name = tea_check_string(T, 1);
    CType match = { .type = CTYPE_FUNC };
    CType* ct;
    void* sym;

    tea_get_udvalue(T, 0, CLIB_CACHE);
    if(tea_get_key(T, -1, name))
        goto done;

    tea_get_fieldp(T, TEA_REGISTRY_INDEX, &cfunc_registry);
    if(!tea_get_key(T, -1, name))
        tea_error(T, "missing declaration for function '%s'", name);
    
    match.func = (CFunc*)tea_to_pointer(T, -1);
    tea_pop(T, 2);
    ct = ctype_lookup(T, &match, false);

    sym = clib_index(T, cl, name);

    cdata_ptr_set(cdata_new(T, ct, NULL), sym);
    tea_push_value(T, -1);
    tea_set_key(T, -3, name);

done:
    tea_remove(T, -2);
}

static void ffi_clib_tostring(tea_State* T)
{
    CLibrary* cl = tea_check_udata(T, 0, CLIB_MT);
    clib_tostring(T, cl);
}

static void ffi_clib_gc(tea_State* T)
{
    CLibrary* cl = tea_check_udata(T, 0, CLIB_MT);

    clib_unload(cl);

    tea_push_pointer(T, cl);
    tea_delete_field(T, TEA_REGISTRY_INDEX);
    
    tea_push_nil(T);
}

static const tea_Methods clib_methods[] = {
    { "getattr", "method", ffi_clib_getattr, 2, 0 },
    { "tostring", "method", ffi_clib_tostring, 1, 0 },
    { "gc", "method", ffi_clib_gc, 1, 0 },
    { NULL, NULL }
};

static void ffi_cdef(tea_State* T)
{
    size_t len;
    const char* str = tea_check_lstring(T, 0, &len);
    cparse_decl(T, str, len);
}

static void ffi_load(tea_State* T)
{
    const char* path = tea_check_string(T, 0);
    bool global = tea_opt_bool(T, 1, false);
    clib_load(T, path, global);
}

static void ffi_cnew(tea_State* T)
{
    bool va = true;
    CType* ct = cparse_single(T, &va, false);
    CData* cd = cdata_new(T, ct, NULL);
    int idx = va ? 3 : 2;
    int ninit;

    ninit = tea_get_top(T) - idx;

    if(ninit == 1)
    {
        cdata_from_tea(T, cd->ct, cdata_ptr(cd), idx - 1, false);
    }
    else if(ninit != 0)
    {
        ctype_tostring(T, ct);
        tea_error(T, "too many initializers for '%s'", tea_get_string(T, -1));
    }
}

static void ffi_cast(tea_State* T)
{
    CType* ct = cparse_single(T, NULL, false);
    CData* cd = cdata_new(T, ct, NULL);
    cdata_from_tea(T, ct, cdata_ptr(cd), 1, true);
}

static void ffi_sizeof(tea_State* T)
{
    CType* ct = cparse_single(T, NULL, false);
    tea_push_integer(T, ctype_sizeof(ct));
}

static void ffi_offsetof(tea_State* T)
{
    CType* ct = cparse_single(T, NULL, false);
    char const* name = tea_check_string(T, 1);
    CRecordField** fields;
    int i;

    if(ct->type != CTYPE_RECORD)
        return;

    fields = ct->rc->fields;

    for(i = 0; i < ct->rc->nfield; i++)
    {
        if(!strcmp(fields[i]->name, name))
        {
            tea_push_integer(T, fields[i]->offset);
            return;
        }
    }

    tea_push_nil(T);
}

static void ffi_alignof(tea_State* T)
{
    CType* ct = cparse_single(T, NULL, false);
    unsigned short align = ctype_ft(ct)->alignment;
    tea_push_integer(T, align);
}

static void ffi_istype(tea_State* T)
{
    CType* ct = cparse_single(T, NULL, false);
    CData* cd = tea_check_udata(T, 1, CDATA_MT);
    tea_push_bool(T, ct == cd->ct);
}

static void ffi_typeof(tea_State* T)
{
    cparse_single(T, NULL, true);
}

static void ffi_addressof(tea_State* T)
{
    CData* cd = tea_check_udata(T, 0, CDATA_MT);
    CType match = {
        .type = CTYPE_PTR,
        .ptr = cd->ct
    };
    CType* ct = ctype_lookup(T, &match, false);
    cdata_ptr_set(cdata_new(T, ct, NULL), cdata_ptr(cd));
}

static void ffi_gc(tea_State* T)
{
    tea_check_udata(T, 0, CDATA_MT);
    tea_set_udvalue(T, 0, 0);
}

static void ffi_tonumber(tea_State* T)
{
    CData* cd = tea_check_udata(T, 0, CDATA_MT);
    CType* ct = cd->ct;

    if(ct->type < CTYPE_VOID)
        cdata_to_tea(T, ct, cdata_ptr(cd));
    else
        tea_push_nil(T);
}

static void ffi_string(tea_State* T)
{
    CData* cd = tea_check_udata(T, 0, CDATA_MT);
    CArray* array = NULL;
    CType* ct = cd->ct;
    const char* ptr = (ct->type == CTYPE_PTR) ? cdata_ptr_ptr(cd) : cdata_ptr(cd);
    size_t len;

    if(tea_get_top(T) > 1)
    {
        len = tea_check_integer(T, 1);

        switch(ct->type)
        {
        case CTYPE_PTR:
        case CTYPE_ARRAY:
        case CTYPE_RECORD:
            tea_push_lstring(T, ptr, len);
            return;
        default:
            goto converr;
        }
    }

    switch(ct->type)
    {
    case CTYPE_PTR:
        ct = ct->ptr;
        break;
    case CTYPE_ARRAY:
        array = ct->array;
        ct = array->ct;
        break;
    default:
        goto converr;
    }

    switch(ct->type)
    {
    case CTYPE_VOID:
    case CTYPE_CHAR:
    case CTYPE_UCHAR:
        break;
    default:
        goto converr;
    }

    if(array && array->size)
    {
        char* p = memchr(ptr, '\0', array->ft.size);
        len = p ? p - ptr : array->ft.size;
        tea_push_lstring(T, ptr, len);
    }
    else
    {
        tea_push_string(T, ptr);
    }
    return;

converr:
    __cdata_tostring(T, cd);
    tea_push_fstring(T, "cannot convert '%s' to 'string'", tea_get_string(T, -1));
    tea_arg_check(T, false, 0, tea_get_string(T, -1));
    return;
}

static void ffi_copy(tea_State* T)
{
    CData* cd = tea_check_udata(T, 0, CDATA_MT);
    void* dst = (cdata_type(cd) == CTYPE_PTR) ? cdata_ptr_ptr(cd) : cdata_ptr(cd);
    const void* src;
    size_t len;

    if(tea_get_top(T) < 3)
    {
        src = tea_check_lstring(T, 1, &len);
        memcpy(dst, src, len);
        ((char*)dst)[len++] = '\0';
    }
    else
    {
        len = tea_check_integer(T, 2);

        if(tea_is_string(T, 1))
            src = tea_get_string(T, 1);
        else
            src = cdata_ptr(tea_check_udata(T, 1, CDATA_MT));

        memcpy(dst, src, len);
    }

    tea_push_integer(T, len);
}

static void ffi_fill(tea_State* T)
{
    CData* cd = tea_check_udata(T, 0, CDATA_MT);
    int len = tea_check_integer(T, 1);
    int c = tea_opt_integer(T, 2, 0);
    void* dst = (cdata_type(cd) == CTYPE_PTR) ? cdata_ptr_ptr(cd) : cdata_ptr(cd);
    memset(dst, c, len);
}

static void ffi_errno(tea_State* T)
{
    int cur = errno;

    if(tea_get_top(T) > 0)
        errno = tea_check_integer(T, 0);

    tea_push_integer(T, cur);
}

/* Check string against a linear list of matches */
int cparse_case(const char* str, size_t len, const char* match)
{
    size_t len1;
    int n;
    for(n = 0; (len1 = (int)*match++); n++, match += len1)
    {
        if(len == len1 && !memcmp(match, str, len1))
        return n;
    }
    return -1;
}

static void ffi__abi(tea_State* T)
{
    size_t len;
    const char* str = tea_check_lstring(T, 0, &len);
    int b = cparse_case(str, len,
#if FFI_64
    "\00564bit"
#else
    "\00532bit"
#endif
#if FFI_LE
    "\002le"
#else
    "\002be"
#endif
    ) >= 0;
    tea_push_bool(T, b);
}

static const tea_Reg funcs[] = {
    { "cdef", ffi_cdef, 1, 0 },
    { "load", ffi_load, 1, 1 },
    { "cnew", ffi_cnew, 1, 2 },
    { "cast", ffi_cast, 2, 0 },
    { "typeof", ffi_typeof, 1, 1 },
    { "addressof", ffi_addressof, 1, 0 },
    { "gc", ffi_gc, 2, 0 },
    { "sizeof", ffi_sizeof, 1, 0 },
    { "offsetof", ffi_offsetof, 2, 0 },
    { "alignof", ffi_alignof, 1, 0 },
    { "istype", ffi_istype, 2, 0 },
    { "tonumber", ffi_tonumber, 1, 0 },
    { "string", ffi_string, 1, 1 },
    { "copy", ffi_copy, 2, 1 },
    { "fill", ffi_fill, 2, 1 },
    { "errno", ffi_errno, 0, 1 },
    { "abi", ffi__abi, 1, 0 },
    { NULL, NULL }
};

TEAMOD_API void tea_import_ffi(tea_State* T)
{
    tea_new_map(T);
    tea_set_fieldp(T, TEA_REGISTRY_INDEX, &crecord_registry);

    tea_new_map(T);
    tea_set_fieldp(T, TEA_REGISTRY_INDEX, &carray_registry);

    tea_new_map(T);
    tea_set_fieldp(T, TEA_REGISTRY_INDEX, &cfunc_registry);

    tea_new_map(T);
    tea_set_fieldp(T, TEA_REGISTRY_INDEX, &ctype_registry);

    tea_new_map(T);
    tea_set_fieldp(T, TEA_REGISTRY_INDEX, &ctdef_registry);

    tea_new_map(T);
    tea_set_fieldp(T, TEA_REGISTRY_INDEX, &clib_registry);

    tea_create_class(T, "CType", ctype_methods);
    tea_set_key(T, TEA_REGISTRY_INDEX, CTYPE_MT);

    tea_create_class(T, "CData", cdata_methods);
    tea_set_key(T, TEA_REGISTRY_INDEX, CDATA_MT);

    tea_create_class(T, "CLib", clib_methods);
    tea_set_key(T, TEA_REGISTRY_INDEX, CLIB_MT);

    tea_create_module(T, "ffi", funcs);

    clib_default(T);
    tea_set_attr(T, -2, "C");
}