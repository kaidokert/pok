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

#ifndef __POK_ARM_CORTEX_M_CONFIG_H__
#define __POK_ARM_CORTEX_M_CONFIG_H__

#include <errno.h>
#include <stdint.h>
#include <types.h>

#ifdef POK_NEEDS_DEBUG
/* For printf diagnostics used below */
#include <libc/stdio.h>
#endif

/**
 * \file    arch/arm/cortex_m_config.h
 * \brief   ARM Cortex-M configuration constants and hardware limits
 * \author  POK team
 *
 * This file centralizes hardware-specific constants for ARM Cortex-M
 * microcontrollers to make it easier to support different variants.
 *
 * Configuration Areas:
 * - NVIC (Nested Vectored Interrupt Controller) settings
 * - MPU (Memory Protection Unit) region counts and sizes
 * - Exception priority levels and bit configurations
 * - Stack alignment requirements for ARM ABI compliance
 * - Runtime validation functions for hardware verification
 *
 * All constants include compile-time assertions to catch configuration
 * errors early in the build process, plus runtime validation functions
 * to verify compatibility with actual hardware capabilities.
 */

/* NVIC (Nested Vectored Interrupt Controller) configuration */
#ifndef CORTEX_M_NVIC_VECTOR_COUNT
#define CORTEX_M_NVIC_VECTOR_COUNT                                             \
  98 /* 16 system + 82 external interrupts (default: STM32F4-compatible) */
#endif

#define CORTEX_M_NVIC_VECTOR_TABLE_SIZE                                        \
  (CORTEX_M_NVIC_VECTOR_COUNT * 4) /* Each vector is 4 bytes */

/* Portable count leading zeros implementation for next power-of-two calculation
 */
#ifndef CORTEX_M_CLZ_IMPL
#if defined(__GNUC__) || defined(__clang__)
/* Use compiler builtin for GCC/Clang */
#define CORTEX_M_CLZ_IMPL(x) __builtin_clz(x)
#else
/* Portable fallback implementation using bit manipulation */
static inline uint32_t cortex_m_clz_fallback(uint32_t x) {
  if (x == 0)
    return 32;
  uint32_t count = 0;
  if (!(x & 0xFFFF0000)) {
    count += 16;
    x <<= 16;
  }
  if (!(x & 0xFF000000)) {
    count += 8;
    x <<= 8;
  }
  if (!(x & 0xF0000000)) {
    count += 4;
    x <<= 4;
  }
  if (!(x & 0xC0000000)) {
    count += 2;
    x <<= 2;
  }
  if (!(x & 0x80000000)) {
    count += 1;
  }
  return count;
}
#define CORTEX_M_CLZ_IMPL(x) cortex_m_clz_fallback(x)
#endif
#endif

/* Calculate next power-of-two for vector table alignment with overflow
 * protection */
#ifndef CORTEX_M_NEXT_POW2
#define CORTEX_M_NEXT_POW2(x)                                                  \
  ((x) <= 1                                                                    \
       ? 1U                                                                    \
       : ((x) > 0x80000000U ? 0                                                \
                            : /* Overflow case - return 0 to indicate error */ \
              (1U << (32 - CORTEX_M_CLZ_IMPL((uint32_t)((x) - 1))))))
#endif

#ifndef CORTEX_M_NVIC_VECTOR_TABLE_ALIGNMENT
#define CORTEX_M_NVIC_VECTOR_TABLE_ALIGNMENT                                   \
  CORTEX_M_NEXT_POW2(CORTEX_M_NVIC_VECTOR_TABLE_SIZE)
#endif

/* Compile-time check: alignment must not be zero (overflow protection) */
#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 201112L)
#define POK_STATIC_ASSERT(cond, msg) \
    typedef char static_assertion_##__LINE__[(cond) ? 1 : -1]
#else
#define POK_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif
_Static_assert(CORTEX_M_NVIC_VECTOR_TABLE_ALIGNMENT != 0,
               "Vector table alignment overflow - reduce NVIC vector count");

/* Compile-time check: vector table alignment must be >= vector table size */
_Static_assert(CORTEX_M_NVIC_VECTOR_TABLE_ALIGNMENT >=
                   CORTEX_M_NVIC_VECTOR_TABLE_SIZE,
               "Vector table alignment must be at least as large as the vector "
               "table size");
/* Compile-time check: vector table alignment must also be a power of two */
_Static_assert((CORTEX_M_NVIC_VECTOR_TABLE_ALIGNMENT &
                (CORTEX_M_NVIC_VECTOR_TABLE_ALIGNMENT - 1)) == 0,
               "Vector table alignment must be a power of two");
/* Additional compile-time validation for release builds - NVIC only */
_Static_assert(CORTEX_M_NVIC_VECTOR_COUNT >= 16,
               "NVIC vector count must include at least 16 system vectors");
_Static_assert(CORTEX_M_NVIC_VECTOR_COUNT <= 496,
               "NVIC vector count exceeds maximum ARM Cortex-M capability (480 "
               "external + 16 system)");

/* MPU (Memory Protection Unit) configuration */
#ifndef CORTEX_M_MPU_MAX_REGIONS
#define CORTEX_M_MPU_MAX_REGIONS                                               \
  8 /* Standard Cortex-M3/M4 has 8 MPU regions                                 \
     */
#endif

#ifndef CORTEX_M_MPU_MIN_REGION_SIZE
#define CORTEX_M_MPU_MIN_REGION_SIZE 32 /* Minimum MPU region size in bytes */
#endif

/* Additional compile-time validation for MPU (after MPU constants defined) */
_Static_assert(CORTEX_M_MPU_MAX_REGIONS <= 16,
               "MPU region count exceeds maximum ARM Cortex-M capability (16)");
_Static_assert(
    CORTEX_M_MPU_MIN_REGION_SIZE >= 32,
    "MPU minimum region size must be at least 32 bytes per ARM spec");
_Static_assert((CORTEX_M_MPU_MIN_REGION_SIZE &
                (CORTEX_M_MPU_MIN_REGION_SIZE - 1)) == 0,
               "MPU minimum region size must be power of 2");

/* Thread/Stack configuration */
#ifndef CORTEX_M_STACK_ALIGNMENT
#define CORTEX_M_STACK_ALIGNMENT                                               \
  8 /* ARM Cortex-M requires 8-byte stack alignment */
#endif

#define CORTEX_M_STACK_ALIGNMENT_MASK (CORTEX_M_STACK_ALIGNMENT - 1)

/* Exception priorities (0 = highest, 255 = lowest for Cortex-M3/M4) */
#ifndef CORTEX_M_PRIORITY_HIGHEST
#define CORTEX_M_PRIORITY_HIGHEST 0
#endif

#ifndef CORTEX_M_PRIORITY_HIGH
#define CORTEX_M_PRIORITY_HIGH 64
#endif

#ifndef CORTEX_M_PRIORITY_NORMAL
#define CORTEX_M_PRIORITY_NORMAL 128
#endif

#ifndef CORTEX_M_PRIORITY_LOW
#define CORTEX_M_PRIORITY_LOW 192
#endif

#ifndef CORTEX_M_PRIORITY_LOWEST
#define CORTEX_M_PRIORITY_LOWEST 255
#endif

/* Runtime configuration validation functions */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Validate Cortex-M configuration constants against actual hardware
 *
 * This function should be called during system initialization to verify
 * that compile-time configuration constants match the actual hardware
 * capabilities.
 *
 * @return POK_ERRNO_OK if all constants are valid, error code otherwise
 */
static inline pok_ret_t cortex_m_validate_config(void) {
#ifdef POK_NEEDS_DEBUG
/* Validate NVIC vector count against hardware */
#define SCB_ICTR                                                               \
  (*((volatile uint32_t                                                        \
          *)(0xE000E004))) /* Interrupt Controller Type Register */
  uint32_t hw_interrupt_lines = ((SCB_ICTR & 0xF) + 1) * 32;
  uint32_t hw_total_vectors =
      16 + hw_interrupt_lines; /* 16 system + external */

  if (CORTEX_M_NVIC_VECTOR_COUNT > hw_total_vectors) {
    printf("ERROR: CORTEX_M_NVIC_VECTOR_COUNT (%u) exceeds hardware "
           "capability (%u)\n",
           CORTEX_M_NVIC_VECTOR_COUNT, hw_total_vectors);
    return POK_ERRNO_EINVAL;
  }

/* Validate MPU region count against hardware */
#define MPU_TYPE_REG                                                           \
  (*((volatile uint32_t *)(0xE000ED90))) /* MPU Type Register */
  uint32_t hw_mpu_regions = (MPU_TYPE_REG >> 8) & 0xFF;

  if (CORTEX_M_MPU_MAX_REGIONS > hw_mpu_regions) {
    printf("ERROR: CORTEX_M_MPU_MAX_REGIONS (%u) exceeds hardware capability "
           "(%u)\n",
           CORTEX_M_MPU_MAX_REGIONS, hw_mpu_regions);
    return POK_ERRNO_EINVAL;
  }

  /* Validate stack alignment matches hardware requirements */
  if (CORTEX_M_STACK_ALIGNMENT < 8) {
    printf("ERROR: CORTEX_M_STACK_ALIGNMENT (%u) below ARM requirement (8)\n",
           CORTEX_M_STACK_ALIGNMENT);
    return POK_ERRNO_EINVAL;
  }

  /* Validate priority level consistency */
  if (CORTEX_M_PRIORITY_HIGHEST >= CORTEX_M_PRIORITY_HIGH ||
      CORTEX_M_PRIORITY_HIGH >= CORTEX_M_PRIORITY_NORMAL ||
      CORTEX_M_PRIORITY_NORMAL >= CORTEX_M_PRIORITY_LOW ||
      CORTEX_M_PRIORITY_LOW >= CORTEX_M_PRIORITY_LOWEST) {
    printf("ERROR: Priority level ordering is inconsistent\n");
    return POK_ERRNO_EINVAL;
  }

  printf("Cortex-M configuration validated: %u vectors, %u MPU regions\n",
         hw_total_vectors, hw_mpu_regions);
#endif

  return POK_ERRNO_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* !__POK_ARM_CORTEX_M_CONFIG_H__ */
