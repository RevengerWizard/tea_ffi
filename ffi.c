/*
** FFI library
** ffi.c
*/

#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <ffi.h>

#define TEA_LIB
#include <tea.h>

#include "lex.h"
#include "teax.h"
#include "helper.h"
#include "alloca_compat.h"

#define MAX_RECORD_FIELDS   30
#define MAX_FUNC_ARGS       30

#define CDATA_MT    "cdata"
#define CTYPE_MT    "ctype"
#define CLIB_MT     "clib"

enum
{
    CTYPE_BOOL,

    CTYPE_CHAR,
    CTYPE_UCHAR,

    CTYPE_SHORT,
    CTYPE_USHORT,

    CTYPE_INT,
    CTYPE_UINT,

    CTYPE_LONG,
    CTYPE_ULONG,

    CTYPE_LONGLONG,
    CTYPE_ULONGLONG,

    CTYPE_INT8_T,
    CTYPE_INT16_T,
    CTYPE_INT32_T,
    CTYPE_INT64_T,
    CTYPE_UINT8_T,
    CTYPE_UINT16_T,
    CTYPE_UINT32_T,
    CTYPE_UINT64_T,

    CTYPE_SIZE_T,

    CTYPE_FLOAT,
    CTYPE_DOUBLE,

    CTYPE_VOID,
    CTYPE_RECORD,
    CTYPE_ARRAY,
    CTYPE_PTR,
    CTYPE_FUNC,
};

typedef struct ctype
{
    uint8_t type;
    uint8_t is_const;
    union
    {
        struct carray* array;
        struct crecord* rc;
        struct cfunc* func;
        struct ctype* ptr;
        ffi_type* ft;
    };
} ctype;

typedef struct carray
{
    size_t size;
    ffi_type ft;
    struct ctype* ct;
} carray;

typedef struct crecord_field
{
    struct ctype* ct;
    size_t offset;
    char name[0];
} crecord_field;

typedef struct crecord
{
    ffi_type ft;
    uint8_t mflags;
    uint8_t nfield;
    uint8_t is_union;
    uint8_t anonymous;
    struct crecord_field* fields[0];
} crecord;

typedef struct cfunc
{
    uint8_t va;
    uint8_t narg;
    struct ctype* rtype;
    struct ctype* args[0];
} cfunc;

typedef struct cdata
{
    struct ctype* ct;
    void* ptr;
} cdata;

typedef struct clib
{
    void* h;
} clib;

static const char* crecord_registry;
static const char* carray_registry;
static const char* cfunc_registry;
static const char* ctype_registry;
static const char* ctdef_registry;
static const char* clib_registry;

static bool tea_is_integer(tea_State* T, int idx)
{
    double number;

    if(!tea_is_number(T, idx))
        return false;

    number = tea_to_number(T, idx);

    return floor(number) == number;
}

static void tea_get_fieldp(tea_State* T, int idx, void* p)
{
    tea_push_pointer(T, p);
    tea_get_field(T, idx);
}

static void tea_set_fieldp(tea_State* T, int idx, void* p)
{
    idx = tea_absindex(T, idx);
    tea_push_pointer(T, p);
    tea_push_value(T, -2);
    tea_set_field(T, idx);
    tea_pop(T, 1);
}

static ffi_type* ffi_get_type(size_t size, bool s)
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

static const char* ctype_name(ctype* ct)
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

static ffi_type* ctype_ft(ctype* ct)
{
    switch(ct->type)
    {
    case CTYPE_ARRAY:
        return &ct->array->ft;
    case CTYPE_RECORD:
        return &ct->rc->ft;
    case CTYPE_PTR:
    case CTYPE_FUNC:
        return &ffi_type_pointer;
    default:
        return ct->ft;
    }
}

static inline size_t ctype_sizeof(ctype* ct)
{
    return ctype_ft(ct)->size;
}

static inline int cdata_type(cdata* cd)
{
    return cd->ct->type;
}

static inline void* cdata_ptr(cdata* cd)
{
    return cd->ptr ? cd->ptr : cd + 1;
}

static void* cdata_ptr_ptr(cdata* cd)
{
    int type = cdata_type(cd);

    if(type != CTYPE_PTR && type != CTYPE_FUNC)
        return NULL;

    return *(void**)cdata_ptr(cd);
}

static inline bool ctype_ptr_to(ctype* ct, int type)
{
    return ct->type != CTYPE_PTR ? false : ct->ptr->type == type;
}

static bool ctype_is_int(ctype* ct)
{
    return ct->type < CTYPE_FLOAT;
}

static bool ctype_is_num(ctype* ct)
{
    return ct->type < CTYPE_VOID;
}

static void cdata_ptr_set(cdata* cd, void* ptr)
{
    int type = cdata_type(cd);

    if(type != CTYPE_PTR && type != CTYPE_FUNC)
        return;

    *(void**)cdata_ptr(cd) = ptr;
}

static ctype* ctype_new(tea_State* T, bool keep)
{
    ctype* ct = tea_new_udata(T, sizeof(ctype), CTYPE_MT);
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

static bool ctype_equal(const ctype* ct1, const ctype* ct2)
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

static ctype* ctype_lookup(tea_State* T, ctype* match, bool keep)
{
    ctype* ct;

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

static carray* carray_lookup(tea_State* T, size_t size, ctype* ct)
{
    carray* a;

    tea_get_fieldp(T, TEA_REGISTRY_INDEX, &carray_registry);

    tea_push_nil(T);
    while(tea_next(T, -2) != 0)
    {
        a = (carray*)tea_to_userdata(T, -1);
        if(a->size == size && ctype_equal(a->ct, ct))
        {
            tea_pop(T, 3);
            return a;
        }
        tea_pop(T, 1);
    }

    a = tea_new_userdata(T, sizeof(carray));
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

static const char* cstruct_lookup_name(tea_State* T, crecord* st)
{
    tea_get_fieldp(T, TEA_REGISTRY_INDEX, &crecord_registry);

    tea_push_nil(T);
    while(tea_next(T, -2) != 0)
    {
        if((crecord*)tea_to_pointer(T, -1) == st)
        {
            tea_pop(T, 1);
            tea_remove(T, -2);
            return tea_to_string(T, -1);
        }
        tea_pop(T, 1);
    }
    return NULL;
}

static cdata* cdata_new(tea_State* T, ctype* ct, void* ptr)
{
    cdata* cd = tea_new_udatav(T, sizeof(cdata) + (ptr ? 0 : ctype_sizeof(ct)), 1, CDATA_MT);
    cd->ptr = ptr;
    cd->ct = ct;

    tea_new_map(T);
    tea_set_fieldp(T, TEA_REGISTRY_INDEX, cd);

    if(!ptr)
        memset(cdata_ptr(cd), 0, ctype_sizeof(ct));

    return cd;
}

static void ctype_tostring(tea_State* T, ctype* ct, teaB_buffer* b)
{
    char buf[128];
    int i;

    if(ct->type != CTYPE_PTR && ct->is_const)
        teaB_addstring(b, "const ");

    switch(ct->type)
    {
    case CTYPE_PTR:
        ctype_tostring(T, ct->ptr, b);
        teaB_addchar(b, '*');
        if(ct->is_const)
            teaB_addstring(b, " const");
        break;
    case CTYPE_ARRAY:
        ctype_tostring(T, ct->array->ct, b);
        snprintf(buf, sizeof(buf), "%llu", ct->array->size);
        teaB_addchar(b, '[');
        teaB_addstring(b, buf);
        teaB_addchar(b, ']');
        break;
    case CTYPE_FUNC:
        ctype_tostring(T, ct->func->rtype, b);
        teaB_addstring(b, " (");
        for(i = 0; i < ct->func->narg; i++)
        {
            if(i > 0)
                teaB_addchar(b, ',');
            ctype_tostring(T, ct->func->args[i], b);
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

static void __cdata_tostring(tea_State* T, cdata* cd)
{
    void* ptr = (cdata_type(cd) == CTYPE_PTR) ? cdata_ptr_ptr(cd) : cdata_ptr(cd);
    teaB_buffer b;

    teaB_buffinit(T, &b);
    teaB_addstring(&b, "cdata<");
    ctype_tostring(T, cd->ct, &b);
    const char* s = tea_push_fstring(T, ">: %p", ptr);
    teaB_addstring(&b, s);
    teaB_pushresult(&b);
}

static void ffi_cdata_tostring(tea_State* T)
{
    cdata* cd = tea_check_udata(T, 0, CDATA_MT);
    __cdata_tostring(T, cd);
}

static void __ctype_tostring(tea_State* T, ctype* ct)
{
    teaB_buffer b;
    teaB_buffinit(T, &b);
    ctype_tostring(T, ct, &b);
    teaB_pushresult(&b);   
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

static void cdata_to_tea(tea_State* T, ctype* ct, void* ptr)
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

static void cdata_from_tea(tea_State* T, ctype* ct, void* ptr, int idx, bool cast);

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

static bool cdata_from_tea_num(tea_State* T, ctype* ct, void* ptr, int idx, bool cast)
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

static bool cdata_from_tea_cdata_ptr(tea_State* T, ctype* ct, void* ptr,
                ctype* from_ct, void* from_ptr, bool cast)
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

static bool cdata_from_tea_cdata(tea_State* T, ctype* ct, void* ptr, int idx, bool cast)
{
    cdata* cd = (cdata*)tea_to_userdata(T, idx);

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

static void cdata_from_tea_list(tea_State* T, ctype* ct, void* ptr, int idx, bool cast)
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

static void cdata_from_tea_map(tea_State* T, ctype* ct, void* ptr, int idx, bool cast)
{
    int i = 0;
    while(i < ct->rc->nfield)
    {
        crecord_field* field = ct->rc->fields[i++];

        if(tea_get_key(T, idx, field->name))
        {
            cdata_from_tea(T, field->ct, ((char*)(ptr)) + field->offset, -1, cast);
            tea_pop(T, 1);
        }
    }
}

static void cdata_from_tea(tea_State* T, ctype* ct, void* ptr, int idx, bool cast)
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
        cdata* cd = tea_to_userdata(T, idx);
        __ctype_tostring(T, cd->ct);
        __ctype_tostring(T, ct);
        tea_push_fstring(T, "cannot convert '%s' to '%s'", tea_get_string(T, -2), tea_get_string(T, -1));
    }
    else
    {
        __ctype_tostring(T, ct);
        tea_push_fstring(T, "cannot convert '%s' to '%s'", tea_typeof(T, idx), tea_get_string(T, -1));
    }
    tea_arg_error(T, idx, tea_get_string(T, -1));
}

static void cdata_index_ptr(tea_State* T, cdata* cd, ctype* ct, bool to)
{
    void* ptr = (cdata_type(cd) == CTYPE_PTR) ? cdata_ptr_ptr(cd) : cdata_ptr(cd);
    int idx;

    if(ct->type == CTYPE_VOID)
    {
        __ctype_tostring(T, cd->ct);
        tea_error(T, "ctype '%s' cannot be indexed", tea_get_string(T, -1));
    }

    if(!tea_is_number(T, 1))
    {
        __ctype_tostring(T, cd->ct);
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
    cdata* cd = tea_check_udata(T, 0, CDATA_MT);
    ctype* ct = cd->ct;

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
    __ctype_tostring(T, cd->ct);
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

static crecord_field* cdata_crecord_find_field(
        crecord_field** fields, int nfield, const char* name, size_t* offset)
{
    int i;
    for(i = 0; i < nfield; i++)
    {
        crecord_field* field = fields[i];

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

static void cdata_index_crecord(tea_State* T, cdata* cd, ctype* ct, bool to)
{
    void* ptr = (cdata_type(cd) == CTYPE_PTR) ? cdata_ptr_ptr(cd) : cdata_ptr(cd);
    crecord* rc = ct->rc;
    crecord_field* field;
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
        __ctype_tostring(T, ct);
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
    cdata* cd = tea_check_udata(T, 0, CDATA_MT);
    ctype* ct = cd->ct;

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
    __ctype_tostring(T, cd->ct);
    tea_error(T, "cannot get attribute of ctype '%s'", tea_get_string(T, -1));
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

    cdata* cd = tea_check_udata(T, 0, CDATA_MT);
    int type = cdata_type(cd);
    cdata* a;
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

static ffi_type* tea_to_vararg(tea_State* T, int idx)
{
    cdata* cd;

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
    cdata* cd = tea_check_udata(T, 0, CDATA_MT);
    ffi_type* args[MAX_FUNC_ARGS] = {0};
    void* values[MAX_FUNC_ARGS] = {0};
    ctype* ct = cd->ct;
    int i, status, narg;
    cfunc* func;
    ctype* rtype;
    ffi_cif cif;
    void* sym;

    if(ct->type != CTYPE_FUNC)
    {
        __ctype_tostring(T, ct);
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
            args[i] = tea_to_vararg(T, i + 1);
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
    cdata* cd = tea_check_udata(T, 0, CDATA_MT);

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
    ctype* ct = tea_check_udata(T, 0, CTYPE_MT);
    tea_push_literal(T, "ctype<");
    __ctype_tostring(T, ct);
    tea_push_literal(T, ">");
    tea_concat(T, 3);
}

static void ffi_ctype_gc(tea_State* T)
{
    ctype* ct = tea_check_udata(T, 0, CTYPE_MT);
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

static void clib_load(tea_State* T, const char* path, bool global)
{
    clib* lib;

#ifdef _WIN32
    HMODULE h;
    if(path)
    {
        h = LoadLibraryA(path);
        if(!h)
        {
            DWORD error = GetLastError();
            char* message;
            FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPSTR)&message,
                0,
                NULL
            );
            tea_push_fstring(T, "LoadLibrary failed: %s", message);
            LocalFree(message);
            tea_throw(T);
            return;
        }
    }
    else
    {
        h = GetModuleHandle(NULL);
    }
#else
    void* h;
    if(path)
    {
        h = dlopen(path, RTLD_LAZY | (global ? RTLD_GLOBAL : RTLD_LOCAL));
        if(!h)
        {
            const char* err = dlerror();
            tea_error(T, err ? err : "dlopen() failed");
            return;
        }
    }
    else
    {
        h = RTLD_DEFAULT;
    }
#endif

    lib = tea_new_udata(T, sizeof(clib), CLIB_MT);
    lib->h = h;
    
    tea_new_map(T);
    tea_set_fieldp(T, TEA_REGISTRY_INDEX, lib);
    
    if(global)
    {
        tea_get_fieldp(T, TEA_REGISTRY_INDEX, &clib_registry);
        tea_push_value(T, -2);
        tea_set_fieldp(T, -2, lib);
        tea_pop(T, 1);
    }
}

static void ffi_clib_getattr(tea_State* T)
{
    clib* lib = tea_check_udata(T, 0, CLIB_MT);
    const char* name = tea_check_string(T, 1);
    ctype match = { .type = CTYPE_FUNC };
    ctype* ct;
    void* sym;

    tea_get_fieldp(T, TEA_REGISTRY_INDEX, lib);
    if(tea_get_key(T, -1, name))
        goto done;

    tea_get_fieldp(T, TEA_REGISTRY_INDEX, &cfunc_registry);
    if(!tea_get_key(T, -1, name))
        tea_error(T, "missing declaration for function '%s'", name);
    
    match.func = (cfunc*)tea_to_pointer(T, -1);
    tea_pop(T, 2);
    ct = ctype_lookup(T, &match, false);

#ifdef _WIN32
    sym = (void*)GetProcAddress(lib->h, name);
#else
    sym = dlsym(lib->h, name);
#endif

    if(!sym)
#ifdef _WIN32
        tea_error(T, "undefined function '%s' (error: %d)", name, GetLastError());
#else
        tea_error(T, "undefined function '%s'", name);
#endif

    cdata_ptr_set(cdata_new(T, ct, NULL), sym);
    tea_push_value(T, -1);
    tea_set_key(T, -3, name);

done:
    tea_remove(T, -2);
}

static void ffi_clib_tostring(tea_State* T)
{
    clib* lib = tea_check_udata(T, 0, CLIB_MT);

#ifdef _WIN32
    if(lib->h == GetModuleHandle(NULL))
        tea_push_literal(T, "library: default");
    else
        tea_push_fstring(T, "library: %p", lib->h);
#else
    if(lib->h == RTLD_DEFAULT)
        tea_push_literal(T, "library: default");
    else
        tea_push_fstring(T, "library: %p", lib->h);
#endif
}

static void ffi_clib_gc(tea_State* T)
{
    clib* lib = tea_check_udata(T, 0, CLIB_MT);
    void* h = lib->h;

#ifdef _WIN32
    if(h != GetModuleHandle(NULL))
        FreeLibrary(h);
#else
    if(h != RTLD_DEFAULT)
        dlclose(h);
#endif

    tea_push_pointer(T, lib);
    tea_delete_field(T, TEA_REGISTRY_INDEX);
    
    tea_push_nil(T);
}

static const tea_Methods clib_methods[] = {
    { "getattr", "method", ffi_clib_getattr, 2, 0 },
    { "tostring", "method", ffi_clib_tostring, 1, 0 },
    { "gc", "method", ffi_clib_gc, 1, 0 },
    { NULL, NULL }
};

static void cparse_expected_error(tea_State* T, int tok, const char* s)
{
    if(tok)
        tea_error(T, "%d:'%s' expected before '%s'", yyget_lineno(), s, yyget_text());
    else
        tea_error(T, "%d:identifier expected", yyget_lineno());
}

static void ctype_to_ptr(tea_State* T, ctype* ct)
{
    ctype* ptr = ctype_lookup(T, ct, false);
    ct->type = CTYPE_PTR;
    ct->is_const = false;
    ct->ptr = ptr;
}

extern char* lex_err;

static int cparse_check_tok(tea_State* T, int tok)
{
    if(!tok && lex_err)
        return tea_error(T, "%d:%s", yyget_lineno(), lex_err);
    return tok;
}

static int cparse_pointer(tea_State* T, int tok, ctype* ct)
{
    while(cparse_check_tok(T, tok) == '*')
    {
        ctype_to_ptr(T, ct);
        tok = yylex();
    }

    if(cparse_check_tok(T, tok) == TOK_CONST)
    {
        ct->is_const = true;
        tok = yylex();
    }

    return tok;
}

static int cparse_array(tea_State* T, int tok, bool* flexible, int* size)
{
    *size = -1;

    if(cparse_check_tok(T, tok) != '[')
    {
        *flexible = false;
        return tok;
    }

    tok = yylex();

    if(!*flexible && cparse_check_tok(T, tok) != TOK_INTEGER)
        return tea_error(T, "%d:flexible array not supported at here", yyget_lineno());

    *flexible = false;

    if(cparse_check_tok(T, tok) == TOK_INTEGER || cparse_check_tok(T, tok) == '?')
    {
        if(cparse_check_tok(T, tok) == TOK_INTEGER)
        {
            *size = atoi(yyget_text());
            if(*size < 0)
                return tea_error(T, "%d:size of array is negative", yyget_lineno());
        }
        else
        {
            *flexible = true;
        }
        tok = yylex();
    }
    else
    {
        *flexible = true;
    }

    if(cparse_check_tok(T, tok) != ']')
        cparse_expected_error(T, tok, "]");

    return yylex();
}

static int cparse_basetype(tea_State* T, int tok, ctype* ct);

static void init_ft_struct(tea_State* T, ffi_type* ft, ffi_type** elements, size_t* offsets)
{
    int status;

    ft->type = FFI_TYPE_STRUCT;
    ft->elements = elements;

    status = ffi_get_struct_offsets(FFI_DEFAULT_ABI, ft, offsets);
    if(status)
        tea_error(T, "ffi_get_struct_offsets fail: %d", status);
}

static void cparse_new_array(tea_State* T, size_t array_size, ctype* ct)
{
    carray* a = carray_lookup(T, array_size, ct);

    ct->type = CTYPE_ARRAY;
    ct->is_const = false;
    ct->array = a;
}

static void check_void_forbidden(tea_State* T, ctype* ct, int tok)
{
    if (ct->type != CTYPE_VOID)
        return;

    if(tok)
        tea_error(T, "%d:void type in forbidden context near '%s'",
                yyget_lineno(), yyget_text());
    else
        tea_error(T, "%d:void type in forbidden context", yyget_lineno());
}

static int cparse_record(tea_State* T, ctype* ct, bool is_union);

static int cparse_record_field(tea_State* T, crecord_field** fields)
{
    int nfield = 0;
    int tok, i;

    while(true)
    {
        crecord_field* field;
        ctype bt = {0}, ct;
        bool flexible = false;
        int array_size;
        char* name;

        tok = yylex();

        if(cparse_check_tok(T, tok) == '}')
            return nfield;

        if(cparse_check_tok(T, tok) == TOK_STRUCT || cparse_check_tok(T, tok) == TOK_UNION)
        {
            tok = cparse_record(T, &bt, cparse_check_tok(T, tok) == TOK_UNION);
            if(tok == ';')
            {
                field = calloc(1, sizeof(crecord_field) + 1);
                ct = bt;
                goto add;
            }
        }
        else
        {
            tok = cparse_basetype(T, tok, &bt);
        }

again:
        ct = bt;

        tok = cparse_pointer(T, tok, &ct);

        check_void_forbidden(T, &ct, tok);

        if(cparse_check_tok(T, tok) != TOK_NAME)
            cparse_expected_error(T, tok, "identifier");

        name = yyget_text();

        for(i = 0; i < nfield; i++)
            if(!strcmp(fields[i]->name, name))
                return tea_error(T, "%d:duplicate member'%s'", yyget_lineno(), name);

        field = calloc(1, sizeof(crecord_field) + yyget_leng() + 1);
        memcpy(field->name, name, yyget_leng());

        tok = cparse_array(T, yylex(), &flexible, &array_size);

        if(array_size >= 0)
            cparse_new_array(T, array_size, &ct);

add:
        field->ct = ctype_lookup(T, &ct, false);
        fields[nfield++] = field;

        if(cparse_check_tok(T, tok) == ',')
        {
            tok = yylex();
            goto again;
        }

        if(cparse_check_tok(T, tok) != ';')
            cparse_expected_error(T, tok, ";");
    }
}

static inline bool ctype_is_zero_array(ctype* ct)
{
    return ct->type == CTYPE_ARRAY && ct->array->size == 0;
}

static int cparse_record(tea_State* T, ctype* ct, bool is_union)
{
    bool named = false;
    int tok = yylex();

    ct->type = CTYPE_RECORD;

    if(cparse_check_tok(T, tok) == TOK_NAME)
    {
        named = true;
        tea_push_string(T, yyget_text());
        tok = yylex();
    }

    if(cparse_check_tok(T, tok) == '{')
    {
        crecord_field* fields[MAX_RECORD_FIELDS];
        size_t offsets[MAX_RECORD_FIELDS];
        ffi_type** elements;
        size_t nfield = 0;
        int i, j, nelement;

        if(named)
        {
            tea_get_fieldp(T, TEA_REGISTRY_INDEX, &crecord_registry);
            tea_push_value(T, -2);
            if(tea_get_field(T, -2))
                return tea_error(T, "%d:redefinition of symbol '%s'", yyget_lineno(), tea_to_string(T, -3));

            tea_pop(T, 1);
        }

        nfield = cparse_record_field(T, fields);

        if(is_union)
        {
            nelement = 2;
        }
        else
        {
            nelement = nfield + 1;

            for(i = 0; i < nfield; i++)
            {
                if(ctype_is_zero_array(fields[i]->ct))
                    nelement--;
            }
        }

        ct->rc = calloc(1, sizeof(crecord)
                        + sizeof(crecord_field*) * nfield
                        + sizeof(ffi_type*) * nelement);

        memcpy(ct->rc->fields, fields, sizeof(crecord_field*) * nfield);

        if(named)
        {
            tea_push_value(T, -2);
            tea_push_pointer(T, ct->rc);
            tea_set_field(T, -3);
            tea_pop(T, 2);
        }
        else
        {
            ct->rc->anonymous = true;
        }

        ct->rc->is_union = is_union;
        ct->rc->nfield = nfield;

        elements = (ffi_type**)&ct->rc->fields[nfield];

        if(is_union)
        {
            for(i = 0; i < nfield; i++)
            {
                if(!ctype_is_zero_array(fields[i]->ct))
                {
                    ffi_type* ft = ctype_ft(fields[i]->ct);
                    if(i == 0 || ft->size > elements[0]->size)
                        elements[0] = ft;
                }
            }

            if(!elements[0])
                nelement--;
        }
        else {
            for(i = 0, j = 0; i < nfield; i++)
            {
                if(ctype_is_zero_array(fields[i]->ct))
                    continue;
                elements[j++] = ctype_ft(fields[i]->ct);
            }
        }

        if(nelement > 1)
            init_ft_struct(T, &ct->rc->ft, elements, offsets);

        if(!is_union)
        {
            for(i = 0, j = 0; i < nfield; i++)
            {
                if(ctype_is_zero_array(fields[i]->ct))
                {
                    if(i > 0)
                        ct->rc->fields[i]->offset = fields[i - 1]->offset + ctype_sizeof(fields[i - 1]->ct);
                }
                else
                {
                    ct->rc->fields[i]->offset = offsets[j++];
                }
            }
        }

        return yylex();
    }
    else
    {
        if(!named)
            cparse_expected_error(T, tok, "identifier");

        tea_get_fieldp(T, TEA_REGISTRY_INDEX, &crecord_registry);
        tea_push_value(T, -2);
        if(!tea_get_field(T, -2))
            return tea_error(T, "%d:undeclared of symbol '%s", yyget_lineno(), tea_to_string(T, -3));

        ct->rc = (crecord*)tea_to_pointer(T, -1);
        tea_pop(T, 3);
    }

    return tok;
}

static int cparse_squals(int type, int squals, ctype* ct, ffi_type* s, ffi_type* u)
{
    ct->type = s ? ++type : type;
    ct->ft = squals == TOK_SIGNED ? s : u;
    return yylex();
}

static int cparse_basetype(tea_State* T, int tok, ctype* ct)
{
    ct->is_const = false;

    if(cparse_check_tok(T, tok) == TOK_CONST)
    {
        ct->is_const = true;
        tok = yylex();
    }

    if(cparse_check_tok(T, tok) == TOK_SIGNED || cparse_check_tok(T, tok) == TOK_UNSIGNED)
    {
        int squals = tok;

        tok = yylex();

        switch(tok) {
        case TOK_CHAR:
            tok = cparse_squals(CTYPE_CHAR, squals, ct, &ffi_type_schar, &ffi_type_uchar);
            break;
        case TOK_SHORT:
            tok = cparse_squals(CTYPE_SHORT, squals, ct, &ffi_type_sshort, &ffi_type_ushort);
            break;
        case TOK_INT:
            tok = cparse_squals(CTYPE_INT, squals, ct, &ffi_type_sint, &ffi_type_uint);
            break;
        case TOK_LONG:
            tok = cparse_squals(CTYPE_LONG, squals, ct, &ffi_type_slong, &ffi_type_ulong);
            break;
        default:
            ct->type = CTYPE_INT;
            ct->ft = (squals == TOK_SIGNED) ? &ffi_type_sint : &ffi_type_uint;
            break;
        }
    }
    else if(cparse_check_tok(T, tok) == TOK_STRUCT || cparse_check_tok(T, tok) == TOK_UNION)
    {
        tok = cparse_record(T, ct, cparse_check_tok(T, tok) == TOK_UNION);
    }
    else
    {
#define INIT_TYPE(t1, t2) \
        ct->type = t1; \
        ct->ft = &t2; \
        break

#define INIT_TYPE_T(t1, t2, s) \
        ct->type = t1; \
        ct->ft = ffi_get_type(sizeof(t2), s); \
        break

        switch(tok)
        {
        case TOK_VOID:
            INIT_TYPE(CTYPE_VOID, ffi_type_void);
        case TOK_BOOL:
            INIT_TYPE(CTYPE_BOOL, ffi_type_sint8);
        case TOK_CHAR:
            INIT_TYPE(CTYPE_CHAR, ffi_type_schar);
        case TOK_SHORT:
            INIT_TYPE(CTYPE_SHORT, ffi_type_sshort);
        case TOK_INT:
            INIT_TYPE(CTYPE_INT, ffi_type_sint);
        case TOK_LONG:
            INIT_TYPE(CTYPE_LONG, ffi_type_slong);
        case TOK_FLOAT:
            INIT_TYPE(CTYPE_FLOAT, ffi_type_float);
        case TOK_DOUBLE:
            INIT_TYPE(CTYPE_DOUBLE, ffi_type_double);
        case TOK_INT8_T:
            INIT_TYPE_T(CTYPE_INT8_T, int8_t, true);
        case TOK_INT16_T:
            INIT_TYPE_T(CTYPE_INT16_T, int16_t, true);
        case TOK_INT32_T:
            INIT_TYPE_T(CTYPE_INT32_T, int32_t, true);
        case TOK_INT64_T:
            INIT_TYPE_T(CTYPE_INT64_T, int64_t, true);
        case TOK_UINT8_T:
            INIT_TYPE_T(CTYPE_UINT8_T, uint8_t, false);
        case TOK_UINT16_T:
            INIT_TYPE_T(CTYPE_UINT16_T, uint16_t, false);
        case TOK_UINT32_T:
            INIT_TYPE_T(CTYPE_UINT32_T, uint32_t, false);
        case TOK_UINT64_T:
            INIT_TYPE_T(CTYPE_UINT64_T, uint64_t, false);
        case TOK_SIZE_T:
            INIT_TYPE_T(CTYPE_SIZE_T, size_t, false);
        case TOK_NAME:
            tea_get_fieldp(T, TEA_REGISTRY_INDEX, &ctdef_registry);
            if(tea_get_key(T, -1, yyget_text()))
            {
                *ct = *(ctype*)tea_to_userdata(T, -1);
                tea_pop(T, 2);
                break;
            }
        default:
            return tea_error(T, "%d:unknown type name '%s'", yyget_lineno(), yyget_text());
        }
        tok = yylex();
#undef INIT_TYPE
#undef INIT_TYPE_T
    }

    if(cparse_check_tok(T, tok) == TOK_INT)
    {
        switch(ct->type)
        {
        case CTYPE_LONG:
        case CTYPE_ULONG:
            tok = yylex();
            break;
        }
    }
    else if(cparse_check_tok(T, tok) == TOK_LONG)
    {
        switch (ct->type)
        {
        case CTYPE_INT:
            ct->type = CTYPE_LONG;
            ct->ft = &ffi_type_slong;
            tok = yylex();
            break;
        case CTYPE_UINT:
            ct->type = CTYPE_ULONG;
            ct->ft = &ffi_type_ulong;
            tok = yylex();
            break;
        case CTYPE_LONG:
            ct->type = CTYPE_LONGLONG;
            ct->ft = &ffi_type_sint64;
            tok = yylex();
            break;
        case CTYPE_ULONG:
            ct->type = CTYPE_ULONGLONG;
            ct->ft = &ffi_type_uint64;
            tok = yylex();
            break;
        }
    }

    if(cparse_check_tok(T, tok) == TOK_CONST)
    {
        ct->is_const = true;
        tok = yylex();
    }

    return tok;
}

static int cparse_function(tea_State* T, int tok, ctype* rtype)
{
    ctype args[MAX_FUNC_ARGS] = {0};
    cfunc *func;
    int i, narg = 0;
    bool va = false;

    tok = cparse_pointer(T, tok, rtype);

    if(cparse_check_tok(T, tok) != TOK_NAME)
        cparse_expected_error(T, tok, "identifier");

    tea_push_string(T, yyget_text());

    tea_get_fieldp(T, TEA_REGISTRY_INDEX, &cfunc_registry);
    tea_push_value(T, -2);
    if(tea_get_field(T, -2))
    {
        return tea_error(T, "%d:redefinition of function '%s'", yyget_lineno(), tea_to_string(T, -3));
    }

    tea_pop(T, 1);

    tok = yylex();

    if(cparse_check_tok(T, tok) != '(')
        cparse_expected_error(T, tok, "(");

    while(true)
    {
        bool flexible = true;
        int array_size;

        tok = yylex();
        if(cparse_check_tok(T, tok) == ')')
            break;

        if(cparse_check_tok(T, tok) == TOK_STRUCT || cparse_check_tok(T, tok) == TOK_UNION)
        {
            tok = cparse_record(T, &args[narg], cparse_check_tok(T, tok) == TOK_UNION);
        }
        else if(cparse_check_tok(T, tok) == TOK_VAL)
        {
            tok = yylex();
            if(cparse_check_tok(T, tok) != ')')
                cparse_expected_error(T, tok, ")");
            va = true;
            break;
        }
        else
        {
            tok = cparse_basetype(T, tok, &args[narg]);
        }

        tok = cparse_pointer(T, tok, &args[narg]);

        if(cparse_check_tok(T, tok) != ')')
            check_void_forbidden(T, &args[narg], tok);
        else
            break;

        if (cparse_check_tok(T, tok) == TOK_NAME)
            tok = yylex();

        tok = cparse_array(T, tok, &flexible, &array_size);

        if(flexible || array_size >= 0)
            ctype_to_ptr(T, &args[narg]);

        narg++;

        if(cparse_check_tok(T, tok) == ')')
            break;

        if(cparse_check_tok(T, tok) != ',')
            cparse_expected_error(T, tok, ",");
    }

    tok = yylex();
    if(cparse_check_tok(T, tok) != ';')
        cparse_expected_error(T, tok, ";");

    func = calloc(1, sizeof(cfunc) + sizeof(ctype*) * narg);

    func->narg = narg;
    func->va = va;

    for(i = 0; i < narg; i++)
    {
        func->args[i] = ctype_lookup(T, &args[i], false);
    }

    func->rtype = ctype_lookup(T, rtype, false);

    tea_push_value(T, -2);
    tea_push_pointer(T, func);
    tea_set_field(T, -3);
    tea_pop(T, 2);

    return 0;
}

static ctype* check_ct(tea_State* T, bool* va, bool keep)
{
    cdata* cd;
    ctype* ct;

    if(tea_is_string(T, 0))
    {
        size_t len;
        const char* str = tea_check_lstring(T, 0, &len);
        bool flexible = false;
        ctype match;
        int array_size;
        int tok;

        yy_scan_bytes(str, len);

        yyset_lineno(0);

        if(va)
            flexible = *va;

        tok = cparse_basetype(T, yylex(), &match);
        tok = cparse_pointer(T, tok, &match);
        tok = cparse_array(T, tok, &flexible, &array_size);

        if(tok)
            tea_error(T, "%d:unexpected '%s'", yyget_lineno(), yyget_text());

        if(flexible || array_size >= 0)
        {
            if(flexible)
            {
                array_size = tea_check_integer(T, 1);
                tea_arg_check(T, array_size > 0, 1, "array size must great than 0");
            }

            cparse_new_array(T, array_size, &match);
        }

        if(va)
            *va = flexible;

        yylex_destroy();

        return ctype_lookup(T, &match, keep);
    }

    if(va)
        *va = false;

    ct = tea_test_udata(T, 0, CTYPE_MT);
    if(ct)
        return ct;

    cd = tea_test_udata(T, 0, CDATA_MT);
    if(cd)
    {
        if(keep)
        {
            tea_get_fieldp(T, TEA_REGISTRY_INDEX, &ctype_registry);
            tea_push_pointer(T, cd->ct);
            tea_get_field(T, -2);
            tea_remove(T, -2);
        }
        return cd->ct;
    }

    tea_type_error(T, 0, "C type");
    return NULL;
}

static void ffi_cdef(tea_State* T)
{
    size_t len;
    const char* str = tea_check_lstring(T, 0, &len);
    int tok;

    yy_scan_bytes(str, len);
    yyset_lineno(0);

    while((tok = yylex()))
    {
        bool tdef = false;
        ctype ct;

        if(cparse_check_tok(T, tok) == ';')
            continue;

        if(cparse_check_tok(T, tok) == TOK_TYPEDEF)
        {
            tdef = true;
            tok = yylex();
        }

        tok = cparse_basetype(T, tok, &ct);

        if(tdef)
        {
            const char* name;

            tok = cparse_pointer(T, tok, &ct);

            if(cparse_check_tok(T, tok) != TOK_NAME)
                cparse_expected_error(T, tok, "identifier");

            name = yyget_text();

            tea_get_fieldp(T, TEA_REGISTRY_INDEX, &ctdef_registry);
            if(tea_get_key(T, -1, name))
                tea_error(T, "%d:redefinition of symbol '%s'", yyget_lineno(), name);

            ctype_lookup(T, &ct, true);
            tea_set_key(T, -2, name);
            tea_pop(T, 1);

            if(cparse_check_tok(T, yylex()) != ';')
                cparse_expected_error(T, tok, ";");

            continue;
        }

        if(cparse_check_tok(T, tok) == ';')
            continue;

        cparse_function(T, tok, &ct);
    }

    yylex_destroy();
}

static void ffi_load(tea_State* T)
{
    const char* path = tea_check_string(T, 0);
    bool global = tea_opt_bool(T, 1, 0);
    clib_load(T, path, global);
}

static void ffi_cnew(tea_State* T)
{
    bool va = true;
    ctype* ct = check_ct(T, &va, false);
    cdata* cd = cdata_new(T, ct, NULL);
    int idx = va ? 3 : 2;
    int ninit;

    ninit = tea_get_top(T) - idx;

    if(ninit == 1)
    {
        cdata_from_tea(T, cd->ct, cdata_ptr(cd), idx - 1, false);
    }
    else if(ninit != 0)
    {
        __ctype_tostring(T, ct);
        tea_error(T, "too many initializers for '%s'", tea_get_string(T, -1));
    }
}

static void ffi_cast(tea_State* T)
{
    ctype* ct = check_ct(T, NULL, false);
    cdata* cd = cdata_new(T, ct, NULL);
    cdata_from_tea(T, ct, cdata_ptr(cd), 1, true);
}

static void ffi_sizeof(tea_State* T)
{
    ctype* ct = check_ct(T, NULL, false);
    tea_push_integer(T, ctype_sizeof(ct));
}

static void ffi_offsetof(tea_State* T)
{
    ctype* ct = check_ct(T, NULL, false);
    char const* name = tea_check_string(T, 1);
    crecord_field** fields;
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

static void ffi_istype(tea_State* T)
{
    ctype* ct = check_ct(T, NULL, false);
    cdata* cd = tea_check_udata(T, 1, CDATA_MT);
    tea_push_bool(T, ct == cd->ct);
}

static void ffi_typeof(tea_State* T)
{
    check_ct(T, NULL, true);
}

static void ffi_addressof(tea_State* T)
{
    cdata* cd = tea_check_udata(T, 0, CDATA_MT);
    ctype match = {
        .type = CTYPE_PTR,
        .ptr = cd->ct
    };
    ctype* ct = ctype_lookup(T, &match, false);
    cdata_ptr_set(cdata_new(T, ct, NULL), cdata_ptr(cd));
}

static void ffi_gc(tea_State* T)
{
    tea_check_udata(T, 0, CDATA_MT);
    tea_set_udvalue(T, 0, 0);
}

static void ffi_tonumber(tea_State* T)
{
    cdata* cd = tea_check_udata(T, 0, CDATA_MT);
    ctype* ct = cd->ct;

    if(ct->type < CTYPE_VOID)
        cdata_to_tea(T, ct, cdata_ptr(cd));
    else
        tea_push_nil(T);
}

static void ffi_string(tea_State* T)
{
    cdata* cd = tea_check_udata(T, 0, CDATA_MT);
    carray* array = NULL;
    ctype* ct = cd->ct;
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
    cdata* cd = tea_check_udata(T, 0, CDATA_MT);
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
    cdata* cd = tea_check_udata(T, 0, CDATA_MT);
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
    { "istype", ffi_istype, 2, 0 },
    { "tonumber", ffi_tonumber, 1, 0 },
    { "string", ffi_string, 1, 1 },
    { "copy", ffi_copy, 2, 1 },
    { "fill", ffi_fill, 2, 1 },
    { "errno", ffi_errno, 0, 1 },
    { NULL, NULL }
};

TEA_API void tea_import_ffi(tea_State* T)
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

    tea_create_class(T, "ctype", ctype_methods);
    tea_set_key(T, TEA_REGISTRY_INDEX, CTYPE_MT);

    tea_create_class(T, "cdata", cdata_methods);
    tea_set_key(T, TEA_REGISTRY_INDEX, CDATA_MT);

    tea_create_class(T, "clib", clib_methods);
    tea_set_key(T, TEA_REGISTRY_INDEX, CLIB_MT);

    tea_create_module(T, "ffi", funcs);

    clib_load(T, NULL, true);
    tea_set_attr(T, -2, "C");
}