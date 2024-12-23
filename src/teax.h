/*
** Teascript C API extensions
** teax.h
*/

#ifndef _TEAX_H
#define _TEAX_H

#include <tea.h>

#define BUFFERSIZE 8192

typedef struct teaB_buffer
{
    char* p;
    int lvl;
    tea_State* T;
    char buffer[BUFFERSIZE];
} teaB_buffer;

#define teaB_addchar(B, c) \
    ((void)((B)->p < ((B)->buffer+BUFFERSIZE) || teaB_prepbuffer(B)), \
    (*(B)->p++ = (char)(c)))

void teaB_buffinit(tea_State* T, teaB_buffer* B);
char* teaB_prepbuffer(teaB_buffer* B);
void teaB_addlstring(teaB_buffer* B, const char* s, size_t l);
void teaB_addstring(teaB_buffer* B, const char* s);
void teaB_addvalue(teaB_buffer* B);
void teaB_pushresult(teaB_buffer* B);

bool tea_is_integer(tea_State* T, int idx);
void tea_get_fieldp(tea_State* T, int idx, void* p);
void tea_set_fieldp(tea_State* T, int idx, void* p);

#define stack_dump(T, title) \
    do { \
        int top = tea_get_top(T); \
        int i; \
        printf("--------stack dump:%s--------\n", title); \
        for(i = 0; i < top; i++) { \
            int t = tea_get_type(T, i); \
            printf("%2d: ", i); \
            switch(t) { \
            case TEA_TYPE_MAP: \
            case TEA_TYPE_STRING: \
                printf("'%s'", tea_to_string(T, i)); \
                tea_pop(T, 1); \
                break; \
            case TEA_TYPE_BOOL: \
                printf(tea_to_bool(T, i) ? "true" : "false"); \
                break; \
            case TEA_TYPE_NUMBER: \
                printf("%s", tea_to_string(T, i)); \
                tea_pop(T, 1); \
                break; \
            case TEA_TYPE_POINTER: \
                printf("%p", tea_to_pointer(T, i)); \
                break; \
            default: \
                printf("%s", tea_to_string(T, i)); \
                tea_pop(T, 1); \
                break; \
            } \
            printf("\n"); \
        } \
        printf("++++++++++++++++++++++++++\n"); \
    } while(0)

#endif