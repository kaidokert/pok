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

/* ARM Cortex-M types - use POK's own type definitions to avoid conflicts */
#define POK_HAVE_STDINT 0
#define POK_HAVE_STDDEF 0

/* Standard integer types for ARM Cortex-M (32-bit) */
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

/* Pointer and size types for ARM Cortex-M (32-bit architecture) */
#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned int size_t;
#endif

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

#endif /* !__POK_ARM_TYPES_H__ */
