#ifndef ALLOCA_COMPAT_H
#define ALLOCA_COMPAT_H

#if defined(_MSC_VER)
    /* Microsoft Visual C++ */
    #include <malloc.h>
    #if !defined(alloca)
        #define alloca _alloca
    #endif
#elif defined(__GNUC__)
    /* GCC */
    #if defined(__MINGW32__) || defined(__MINGW64__)
        /* MinGW */
        #include <malloc.h>
    #elif defined(__linux__) || defined(__unix__) || defined(__APPLE__)
        /* Linux, Unix, or macOS */
        #if defined(__APPLE__)
            #include <sys/types.h>
        #else
            #include <stdlib.h>
        #endif
        /* Some systems might define alloca in stdlib.h */
        #if !defined(alloca)
            #include <alloca.h>
        #endif
    #endif
#elif defined(__BORLANDC__)
    /* Borland C++ */
    #include <malloc.h>
#else
    /* Handle other compilers */
    #if !defined(alloca)
        #if defined(__has_include)
            #if __has_include(<alloca.h>)
                #include <alloca.h>
            #elif __has_include(<malloc.h>)
                #include <malloc.h>
            #else
                #error "No alloca.h or malloc.h found"
            #endif
        #else
            /* Fallback for compilers without __has_include */
            #include <stdlib.h>
            #if !defined(alloca)
                void *alloca(size_t size);
            #endif
        #endif
    #endif
#endif

/* Safety check */
#if !defined(alloca)
    #error "alloca is not defined after inclusion of all relevant headers"
#endif

#endif /* ALLOCA_COMPAT_H */