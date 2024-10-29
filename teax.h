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

#endif