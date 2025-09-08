/*
 *                               POK header
 *
 * The following file is a part of the POK project. Any modification should
 * be made according to the POK licence. You CANNOT use this file or a part
 * of a file for your own project.
 *
 * For more information on the POK licence, please see our LICENCE FILE
 *
 * Please follow the coding guidelines described in doc/CODING_GUIDELINES
 *
 *                                      Copyright (c) 2007-2025 POK team
 */

#ifndef __POK_ARM_TYPES_H__
#define __POK_ARM_TYPES_H__

/* Standard integer types for ARM Cortex-M */
/* Prefer system headers; fall back only if missing */
#ifdef __has_include
#if __has_include(<stdint.h>)
#include <stdint.h>
#define POK_HAVE_STDINT 1
#endif
#if __has_include(<stddef.h>)
#include <stddef.h>
#define POK_HAVE_STDDEF 1
#endif
#endif
/* Fallback path for compilers without __has_include */
#ifndef POK_HAVE_STDINT
#ifndef __has_include
#include <stdint.h>
#define POK_HAVE_STDINT 1
#endif
#endif
#ifndef POK_HAVE_STDDEF
#ifndef __has_include
#include <stddef.h>
#define POK_HAVE_STDDEF 1
#endif
#endif

/* If stdint.h types are still not available, define them ourselves */
#ifndef UINT8_MAX
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef signed long long int64_t;

#define UINT8_MAX 255U
#define UINT16_MAX 65535U
#define UINT32_MAX 4294967295U
#define INT32_MAX 2147483647
#define INT32_MIN (-2147483647 - 1)
#endif

/* Pointer types for ARM Cortex-M (32-bit architecture) */
/* size_t: prefer stddef.h; else compiler builtin; else 32-bit fallback */
#ifndef POK_HAVE_STDDEF
#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned int size_t;
#endif
#endif

/* uintptr_t/intptr_t: prefer stdint.h; else compiler builtins; else 32-bit
 * fallbacks */
#ifndef POK_HAVE_STDINT
#ifdef __UINTPTR_TYPE__
typedef __UINTPTR_TYPE__ uintptr_t;
#else
typedef unsigned int uintptr_t;
#endif
#ifdef __INTPTR_TYPE__
typedef __INTPTR_TYPE__ intptr_t;
#else
typedef signed int intptr_t;
#endif
#endif

#endif /* !__POK_ARM_TYPES_H__ */
