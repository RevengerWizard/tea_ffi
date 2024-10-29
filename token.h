#ifndef __FFI_TOKEN__
#define __FFI_TOKEN__

enum
{
    TOK_NAME = 256,
    TOK_TYPEDEF,
    TOK_VAL,
    TOK_INTEGER,
    TOK_STRUCT,
    TOK_UNION,

    TOK_CONST,
    TOK_SIGNED,
    TOK_UNSIGNED,

    TOK_BOOL,
    TOK_VOID,
    TOK_CHAR,
    TOK_SHORT,
    TOK_INT,
    TOK_LONG,
    TOK_FLOAT,
    TOK_DOUBLE,

    TOK_INT8_T,
    TOK_INT16_T,
    TOK_INT32_T,
    TOK_INT64_T,
    TOK_UINT8_T,
    TOK_UINT16_T,
    TOK_UINT32_T,
    TOK_UINT64_T,
    
    TOK_SIZE_T,
};

#endif