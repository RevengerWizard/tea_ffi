/*
** arch.h
** Target architecture selection
*/

#ifndef _ARCH_H
#define _ARCH_H

/* -- Target definitions -------------------------------------------------- */

/* Target endianess */
#define FFI_ENDIAN_LE 0
#define FFI_ENDIAN_BE 1

/* Target architectures */
#define FFI_ARCH_X86 1
#define FFI_ARCH_X64 2
#define FFI_ARCH_ARM 3
#define FFI_ARCH_ARM64 4

/* Target OS */
#define FFI_OS_OTHER 0
#define FFI_OS_WINDOWS 1
#define FFI_OS_LINUX 2
#define FFI_OS_MACOSX 3
#define FFI_OS_BSD 4
#define FFI_OS_POSIX 5

/* -- Target detection ---------------------------------------------------- */

/* Select native target if no target defined */
#ifndef FFI_TARGET

#if defined(__i386) || defined(__i386__) || defined(_M_IX86)
#define FFI_TARGET   FFI_ARCH_X86
#elif defined(__x86_64__) || defined(__x86_64) || defined(_M_X64) || defined(_M_AMD64)
#define FFI_TARGET   FFI_ARCH_X64
#elif defined(__arm__) || defined(__arm) || defined(__ARM__) || defined(__ARM)
#define FFI_TARGET   FFI_ARCH_ARM
#elif defined(__aarch64__)
#define FFI_TARGET   FFI_ARCH_ARM64
#else
#error "No support for this architecture (yet)"
#endif

#endif

/* Select target OS if no target OS defined */
#ifndef FFI_OS

#if defined(_WIN32) || defined(_WIN64)
#define FFI_OS       FFI_OS_WINDOWS
#elif defined(__linux__)
#define FFI_OS       FFI_OS_LINUX
#elif defined(__MACH__) && defined(__APPLE__)
#define FFI_OS       FFI_OS_MACOSX
#elif (defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || \
       defined(__NetBSD__) || defined(__OpenBSD__) || \
       defined(__DragonFly__)) && !defined(__ORBIS__)
#define FFI_OS       FFI_OS_BSD
#else
#define FFI_OS       FFI_OS_OTHER
#endif

#endif

#define FFI_TARGET_WINDOWS  (FFI_OS == FFI_OS_WINDOWS)
#define FFI_TARGET_LINUX    (FFI_OS == FFI_OS_LINUX)
#define FFI_TARGET_MACOSX   (FFI_OS == FFI_OS_MACOSX)
#define FFI_TARGET_BSD      (FFI_OS == FFI_OS_BSD)
#define FFI_TARGET_POSIX    (FFI_OS > FFI_OS_WINDOWS)
#define FFI_TARGET_DLOPEN   FFI_TARGET_POSIX

/* -- Arch-specific settings ---------------------------------------------- */

/* Set target architecture properties */
#if FFI_TARGET == FFI_ARCH_X86

#define FFI_ARCH_BITS 32
#define FFI_ARCH_ENDIAN FFI_ENDIAN_LE

#elif FFI_TARGET == FFI_ARCH_X64

#define FFI_ARCH_BITS 64
#define FFI_ARCH_ENDIAN FFI_ENDIAN_LE

#elif FFI_TARGET == FFI_ARCH_ARM

#define FFI_ARCH_BITS 32
#define FFI_ARCH_ENDIAN FFI_ENDIAN_LE

#elif FFI_TARGET == FFI_ARCH_ARM64

#define FFI_ARCH_BITS 64
#if defined(__AARCH64EB__)
#define FFI_ARCH_ENDIAN FFI_ENDIAN_BE
#else
#define FFI_ARCH_ENDIAN FFI_ENDIAN_LE
#endif

#else
#error "No target architecture defined"
#endif

#if FFI_ARCH_ENDIAN == FFI_ENDIAN_BE
#define FFI_LE 0
#define FFI_BE 1
#define FFI_ENDIAN_SELECT(le, be) be
#define FFI_ENDIAN_LOHI(lo, hi) hi lo
#else
#define FFI_LE 1
#define FFI_BE 0
#define FFI_ENDIAN_SELECT(le, be) le
#define FFI_ENDIAN_LOHI(lo, hi) lo hi
#endif

#if FFI_ARCH_BITS == 32
#define FFI_32       1
#define FFI_64       0
#else
#define FFI_32       0
#define FFI_64       1
#endif

#endif