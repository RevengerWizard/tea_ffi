#include <string.h>

#include "teax.h"

#define bufflen(B)	((size_t)((B)->p - (B)->buffer))
#define bufffree(B)	((size_t)(BUFFERSIZE - bufflen(B)))

static int emptybuffer(teaB_buffer* B)
{
    size_t l = bufflen(B);
    if(l == 0)
        return 0;  /* put nothing on stack */
    tea_push_lstring(B->T, B->buffer, l);
    B->p = B->buffer;
    B->lvl++;
    return 1;
}

static void adjuststack(teaB_buffer* B)
{
    if(B->lvl > 1)
    {
        tea_State* T = B->T;
        int toget = 1;  /* number of levels to concat */
        size_t toplen = tea_len(T, -1);
        do
        {
            size_t l = tea_len(T, -(toget+1));
            if(!(B->lvl - toget + 1 >= TEA_MIN_STACK/2 || toplen > l))
                break;
            toplen += l;
            toget++;
        }
        while(toget < B->lvl);
        tea_concat(T, toget);
        B->lvl = B->lvl - toget + 1;
    }
}

void teaB_buffinit(tea_State* T, teaB_buffer* B)
{
    B->T = T;
    B->p = B->buffer;
    B->lvl = 0;
}

char* teaB_prepbuffer(teaB_buffer* B)
{
    if(emptybuffer(B))
        adjuststack(B);
    return B->buffer;
}

void teaB_addlstring(teaB_buffer* B, const char* s, size_t l)
{
    if(l <= bufffree(B))
    {
        memcpy(B->p, s, l);
        B->p += l;
    }
    else
    {
        emptybuffer(B);
        tea_push_lstring(B->T, s, l);
        B->lvl++;
        adjuststack(B);
    }
}

void teaB_addstring(teaB_buffer* B, const char* s)
{
    teaB_addlstring(B, s, strlen(s));
}

void teaB_addvalue(teaB_buffer* B)
{
    tea_State* T = B->T;
    size_t vl;
    const char* s = tea_get_lstring(T, -1, &vl);
    if(vl <= bufffree(B))
    {  /* fit into buffer? */
        memcpy(B->p, s, vl);  /* put it there */
        B->p += vl;
        tea_pop(T, 1);  /* remove from stack */
    }
    else
    {
        if(emptybuffer(B))
            tea_insert(T, -2);  /* put buffer before new value */
        B->lvl++;  /* add new value into B stack */
        adjuststack(B);
    }
}

void teaB_pushresult(teaB_buffer* B)
{
    emptybuffer(B);
    tea_concat(B->T, B->lvl);
    B->lvl = 1;
}