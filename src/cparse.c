/*
** C declaration parser
** tea_cparse.c
*/

#include <stdlib.h>

#include "lex.h"

#include "teax.h"

#include "tea_ffi.h"
#include "ctype.h"
#include "cparse.h"

static void cparse_expected_error(tea_State* T, int tok, const char* s)
{
    if(tok)
        tea_error(T, "%d:'%s' expected before '%s'", yyget_lineno(), s, yyget_text());
    else
        tea_error(T, "%d:identifier expected", yyget_lineno());
}

static void ctype_to_ptr(tea_State* T, CType* ct)
{
    CType* ptr = ctype_lookup(T, ct, false);
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

static int cparse_pointer(tea_State* T, int tok, CType* ct)
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

static int cparse_basetype(tea_State* T, int tok, CType* ct);

static void init_ft_struct(tea_State* T, ffi_type* ft, ffi_type** elements, size_t* offsets)
{
    int status;

    ft->type = FFI_TYPE_STRUCT;
    ft->elements = elements;

    status = ffi_get_struct_offsets(FFI_DEFAULT_ABI, ft, offsets);
    if(status)
        tea_error(T, "ffi_get_struct_offsets fail: %d", status);
}

static void cparse_new_array(tea_State* T, size_t array_size, CType* ct)
{
    CArray* a = carray_lookup(T, array_size, ct);
    ct->type = CTYPE_ARRAY;
    ct->is_const = false;
    ct->array = a;
}

static void check_void_forbidden(tea_State* T, CType* ct, int tok)
{
    if (ct->type != CTYPE_VOID)
        return;

    if(tok)
        tea_error(T, "%d:void type in forbidden context near '%s'",
                yyget_lineno(), yyget_text());
    else
        tea_error(T, "%d:void type in forbidden context", yyget_lineno());
}

static int cparse_record(tea_State* T, CType* ct, bool is_union);

static int cparse_record_field(tea_State* T, CRecordField** fields)
{
    int nfield = 0;
    int tok, i;

    while(true)
    {
        CRecordField* field;
        CType bt = {0}, ct;
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
                field = calloc(1, sizeof(CRecordField) + 1);
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

        field = calloc(1, sizeof(CRecordField) + yyget_leng() + 1);
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

static int cparse_record(tea_State* T, CType* ct, bool is_union)
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
        CRecordField* fields[MAX_RECORD_FIELDS];
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

        ct->rc = calloc(1, sizeof(CRecord)
                        + sizeof(CRecordField*) * nfield
                        + sizeof(ffi_type*) * nelement);

        memcpy(ct->rc->fields, fields, sizeof(CRecordField*) * nfield);

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

        ct->rc = (CRecord*)tea_to_pointer(T, -1);
        tea_pop(T, 3);
    }

    return tok;
}

static int cparse_squals(int type, int squals, CType* ct, ffi_type* s, ffi_type* u)
{
    ct->type = s ? ++type : type;
    ct->ft = squals == TOK_SIGNED ? s : u;
    return yylex();
}

static int cparse_basetype(tea_State* T, int tok, CType* ct)
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
                *ct = *(CType*)tea_to_userdata(T, -1);
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

static void cparse_function(tea_State* T, int tok, CType* rtype)
{
    CType args[MAX_FUNC_ARGS] = {0};
    CFunc *func;
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
        tea_error(T, "%d:redefinition of function '%s'", yyget_lineno(), tea_to_string(T, -3));
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

    func = calloc(1, sizeof(CFunc) + sizeof(CType*) * narg);

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
}

CType* cparse_single(tea_State* T, bool* va, bool keep)
{
    CData* cd;
    CType* ct;

    if(tea_is_string(T, 0))
    {
        size_t len;
        const char* str = tea_check_lstring(T, 0, &len);
        bool flexible = false;
        CType match;
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

void cparse_decl(tea_State* T, const char* p, size_t len)
{
    yy_scan_bytes(p, len);
    yyset_lineno(0);

    int tok;
    while((tok = yylex()))
    {
        bool tdef = false;
        CType ct;

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