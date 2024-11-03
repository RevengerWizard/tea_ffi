#ifndef _HELPER_H
#define _HELPER_H

#include <tea.h>

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