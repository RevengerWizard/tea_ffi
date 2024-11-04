/*
** C type management
** tea_ctype.h
*/

#ifndef _TEA_CTYPE_H
#define _TEA_CTYPE_H

#include <stdint.h>

#include <ffi.h>

#include <tea.h>

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

typedef struct CType
{
    uint8_t type;
    uint8_t is_const;
    union
    {
        struct CArray* array;
        struct CRecord* rc;
        struct CFunc* func;
        struct CType* ptr;
        ffi_type* ft;
    };
} CType;

typedef struct CArray
{
    size_t size;
    ffi_type ft;
    struct CType* ct;
} CArray;

typedef struct CRecordField
{
    struct CType* ct;
    size_t offset;
    char name[0];
} CRecordField;

typedef struct CRecord
{
    ffi_type ft;
    uint8_t mflags;
    uint8_t nfield;
    uint8_t is_union;
    uint8_t anonymous;
    struct CRecordField* fields[0];
} CRecord;

typedef struct CFunc
{
    uint8_t va;
    uint8_t narg;
    struct CType* rtype;
    struct CType* args[0];
} CFunc;

typedef struct CData
{
    struct CType* ct;
    void* ptr;
} CData;

CArray* carray_lookup(tea_State* T, size_t size, CType* ct);
CType* ctype_lookup(tea_State* T, CType* match, bool keep);
bool ctype_equal(const CType* ct1, const CType* ct2);
const char* ctype_name(CType* ct);
void __ctype_tostring(tea_State* T, CType* ct, teaB_buffer* b);
void ctype_tostring(tea_State* T, CType* ct);

static inline ffi_type* ctype_ft(CType* ct)
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

static inline size_t ctype_sizeof(CType* ct)
{
    return ctype_ft(ct)->size;
}

static inline bool ctype_ptr_to(CType* ct, int type)
{
    return ct->type != CTYPE_PTR ? false : ct->ptr->type == type;
}

static inline bool ctype_is_int(CType* ct)
{
    return ct->type < CTYPE_FLOAT;
}

static inline bool ctype_is_num(CType* ct)
{
    return ct->type < CTYPE_VOID;
}

static inline bool ctype_is_zero_array(CType* ct)
{
    return ct->type == CTYPE_ARRAY && ct->array->size == 0;
}

#endif