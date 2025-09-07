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
/* Try to use system stdint.h first, fallback to our own definitions */
#ifdef __has_include
#if __has_include(<stdint.h>)
#include <stdint.h>
#endif
#else
/* Fallback: try standard include locations */
#ifndef _STDINT_H_INCLUDED
#include <stdint.h>
#define _STDINT_H_INCLUDED
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
typedef unsigned int size_t;
typedef unsigned int uintptr_t;
typedef signed int intptr_t;

#endif /* !__POK_ARM_TYPES_H__ */
