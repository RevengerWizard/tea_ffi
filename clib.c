/*
** clib.c
*/

#include <tea.h>

#include "arch.h"

#include "tea_ffi.h"
#include "clib.h"

#if FFI_TARGET_DLOPEN

#include <dlfcn.h>
#include <stdio.h>

#if defined(RTLD_DEFAULT) && !defined(NO_RTLD_DEFAULT)
#define CLIB_DEFHANDLE	RTLD_DEFAULT
#elif LJ_TARGET_OSX || LJ_TARGET_BSD
#define CLIB_DEFHANDLE	((void *)(intptr_t)-2)
#else
#define CLIB_DEFHANDLE	NULL
#endif

static void clib_error_(tea_State* T)
{
    tea_error(T, "%s", dlerror());
}

#define clib_error(T, fmt, name)    clib_error_(T)

#define CLIB_SOPREFIX   "lib"

#if FFI_TARGET_OSX
#define CLIB_SOEXT  "%s.dylib"
#elif FFI_TARGET_CYGWIN
#define CLIB_SOEXT  "%s.dll"
#else
#define CLIB_SOEXT  "%s.so"
#endif

static const char* clib_extname(tea_State* T, const char* name)
{
    if(!strchr(name, '/'))
    {
        if(!strchr(name, '.'))
        {
            name = tea_push_fstring(T, CLIB_SOEXT, name);
            tea_pop(T, 1);
        }
        if(!(name[0] == CLIB_SOPREFIX[0] && name[1] == CLIB_SOPREFIX[1] &&
        name[2] == CLIB_SOPREFIX[2]))
        {
            name = tea_push_fstring(T, CLIB_SOPREFIX "%s", name);
            tea_pop(T, 1);
        }
    }
    return name;
}

/* Check for a recognized ld script line */
static const char* clib_check_lds(tea_State* T, const char* buf)
{
    char *p, *e;
    if ((!strncmp(buf, "GROUP", 5) || !strncmp(buf, "INPUT", 5)) &&
        (p = strchr(buf, '('))) {
        while (*++p == ' ') ;
        for (e = p; *e && *e != ' ' && *e != ')'; e++) ;
        return strdata(lj_str_new(L, p, e-p));
    }
    return NULL;
}

/* Quick and dirty solution to resolve shared library name from ld script */
static const char* clib_resolve_lds(tea_State* T, const char* name)
{
    FILE* fp = fopen(name, "r");
    const char* p = NULL;
    if(fp)
    {
        char buf[256];
        if(fgets(buf, sizeof(buf), fp))
        {
            if(!strncmp(buf, "/* GNU ld script", 16))
            {
                /* ld script magic? */
                while(fgets(buf, sizeof(buf), fp))
                {
                    /* Check all lines. */
                    p = clib_check_lds(L, buf);
                    if(p) break;
                }
            }
            else
            {
                /* Otherwise check only the first line. */
                p = clib_check_lds(L, buf);
            }
        }
        fclose(fp);
    }
    return p;
}

static void* clib_loadlib(tea_State* T, const char* name, bool global)
{
    void* h = dlopen(clib_extname(T, name),
            RTLD_LAZY | (global ? RTLD_GLOBAL : RTLD_LOCAL));
    if(!h) 
    {
        const char* e, *err = dlerror();
        if(err && *err == '/' && (e = strchr(err, ':')) &&
        (name = clib_resolve_lds(T, tea_push_lstring(T, err, (size_t)e-err)))))
        {
            h = dlopen(name, RTLD_LAZY | (global ? RTLD_GLOBAL : RTLD_LOCAL));
            if(h) return h;
            err = dlerror();
        }
        if (!err) err = "dlopen failed";
        tea_error(T, "%s", err);
    }
    return h;
}

static void clib_unloadlib(clib* cl)
{
    if(cl->handle && cl->handle != CLIB_DEFHANDLE)
        dlclose(cl->handle);
}

static void* clib_getsym(clib* cl, const char* name)
{
    void* p = dlsym(cl->handle, name);
    return p;
}

#elif FFI_TARGET_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>

#define CLIB_DEFHANDLE	((void*)-1)

/* Default libraries */
enum
{
    CLIB_HANDLE_EXE,
    CLIB_HANDLE_DLL,
    CLIB_HANDLE_CRT,
    CLIB_HANDLE_KERNEL32,
    CLIB_HANDLE_USER32,
    CLIB_HANDLE_GDI32,
    CLIB_HANDLE_MAX
};

static void* clib_def_handle[CLIB_HANDLE_MAX];

static void clib_error(tea_State* T, const char* fmt, const char *name)
{
    DWORD err = GetLastError();

    char buf[128];
    if(!FormatMessageA(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0, buf, sizeof(buf), NULL))
        buf[0] = '\0';
    tea_push_fstring(T, fmt, name, buf);
    tea_throw(T);
}

static int clib_needext(const char* s)
{
    while(*s)
    {
        if(*s == '/' || *s == '\\' || *s == '.') return 0;
        s++;
    }
    return 1;
}

static const char* clib_extname(tea_State* T, const char* name)
{
    if(clib_needext(name))
    {
        name = tea_push_fstring(T, "%s.dll", name);
    }
    return name;
}

#define FFI_WIN_LOADLIBA(path)  LoadLibraryExA((path), NULL, 0)

static void* clib_loadlib(tea_State* T, const char* name, bool global)
{
    DWORD oldwerr = GetLastError();
    void* h = FFI_WIN_LOADLIBA(clib_extname(T, name)); tea_pop(T, 1);
    if(!h) clib_error(T, "cannot load module " TEA_QS ": %s", name);
    SetLastError(oldwerr);
    (void)global;
    return h;
}

static void clib_unloadlib(clib* cl)
{
    if(cl->handle == CLIB_DEFHANDLE)
    {
        int i;
        for(i = CLIB_HANDLE_KERNEL32; i < CLIB_HANDLE_MAX; i++)
        {
            void* h = clib_def_handle[i];
            if(h)
            {
                clib_def_handle[i] = NULL;
                FreeLibrary((HINSTANCE)h);
            }
        }
    }
    else if(cl->handle)
    {
        FreeLibrary((HINSTANCE)cl->handle);
    }
}

static void* clib_getsym(clib* cl, const char* name)
{
    void* p = NULL;
    if(cl->handle == CLIB_DEFHANDLE)
    {
        /* Search default libraries. */
        int i;
        for(i = 0; i < CLIB_HANDLE_MAX; i++)
        {
            HINSTANCE h = (HINSTANCE)clib_def_handle[i];
            if(!(void*)h)
            {
                /* Resolve default library handles (once). */
                switch(i)
                {
                case CLIB_HANDLE_EXE: 
                    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, NULL, &h); break;
                case CLIB_HANDLE_DLL:
                    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (const char*)clib_def_handle, &h);
                    break;
                case CLIB_HANDLE_CRT:
                    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (const char*)&_fmode, &h);
                    break;
                case CLIB_HANDLE_KERNEL32: h = FFI_WIN_LOADLIBA("kernel32.dll"); break;
                case CLIB_HANDLE_USER32: h = FFI_WIN_LOADLIBA("user32.dll"); break;
                case CLIB_HANDLE_GDI32: h = FFI_WIN_LOADLIBA("gdi32.dll"); break;
                }
                if(!h) continue;
                clib_def_handle[i] = (void*)h;
            }
            p = (void*)GetProcAddress(h, name);
            if(p) break;
        }
    }
    else
    {
        p = (void*)GetProcAddress((HINSTANCE)cl->handle, name);
    }
    return p;
}

#else

#define CLIB_DEFHANDLE  NULL

static void clib_error(tea_State* T, const char* fmt, const char *name)
{
    tea_error(T, fmt, name, "no support for this OS");
}

static void* clib_loadlib(tea_State* T, const char* name, bool global)
{
    tea_error(T, "no support for loading dynamic libraries for this OS");
    (void)name; (void)global;
    return NULL;
}

static void clib_unloadlib(clib* cl)
{
    (void)cl;
}

static void* clib_getsym(clib* cl, const char* name)
{
    (void)cl; (void)name;
    return NULL;
}

#endif

/* Index a C library by name */
void* clib_index(tea_State* T, clib* cl, const char* name)
{
    void* p = clib_getsym(cl, name);
#if FFI_TARGET_WINDOWS
    DWORD oldwerr = GetLastError();
#endif

    if(!p)
	    clib_error(T, "cannot resolve symbol " TEA_QS ": %s", name);

#if FFI_TARGET_WINDOWS
    SetLastError(oldwerr);
#endif

    return p;
}

/* Create a new clib object and push it on the stack */
static clib* clib_new(tea_State* T)
{
    clib* cl = tea_new_udatav(T, sizeof(clib), 1, CLIB_MT);

    tea_new_map(T);
    tea_set_udvalue(T, -2, CLIB_CACHE);

    return cl;
}

/* Load a C library */
clib* clib_load(tea_State* T, const char* name, bool global)
{
    void* handle = clib_loadlib(T, name, global);
    clib* cl = clib_new(T);
    cl->handle = handle;
    return cl;
}

/* Unload a C library */
void clib_unload(clib* cl)
{
    clib_unloadlib(cl);
    cl->handle = NULL;
}

/* Create the default C library object */
void clib_default(tea_State* T)
{
    clib* cl = clib_new(T);
    cl->handle = CLIB_DEFHANDLE;
}

void clib_tostring(tea_State* T, clib* cl)
{
    if(cl->handle == CLIB_DEFHANDLE)
        tea_push_literal(T, "library: default");
    else
        tea_push_fstring(T, "library: %p", cl->handle);
}