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

#ifndef __POK_ARM_MPU_UTILS_H__
#define __POK_ARM_MPU_UTILS_H__

#include "cortex_m_config.h"
#include <types.h>

/* Define UINT32_MAX locally to avoid stdint.h conflicts */
#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFU
#endif

/**
 * \file    arch/arm/mpu_utils.h
 * \brief   Shared MPU alignment and utility functions for ARM Cortex-M
 * \author  POK team
 *
 * This header provides utility functions for ARM Cortex-M Memory Protection
 * Unit operations including alignment calculations, power-of-2 validation, and
 * safe arithmetic operations with overflow protection.
 *
 * Key features:
 * - Power-of-2 size alignment for MPU region requirements
 * - Overflow-safe address alignment functions
 * - Input validation for all utility functions
 * - Support for ARM Cortex-M MPU constraints (minimum 32-byte regions)
 */

#define MPU_MIN_REGION_SIZE CORTEX_M_MPU_MIN_REGION_SIZE

/* Maximum alignable size - prevents overflow in power-of-2 calculations */
#define MPU_MAX_ALIGNABLE_SIZE 0x80000000U

/**
 * Round up size to next power of 2, with minimum of MPU_MIN_REGION_SIZE
 *
 * @param size Size to round up
 * @return Next power of 2 >= size, minimum MPU_MIN_REGION_SIZE
 */
static inline uint32_t mpu_align_size_to_power_of_2(uint32_t size) {
  if (size <= MPU_MIN_REGION_SIZE) {
    return MPU_MIN_REGION_SIZE;
  }

  /* Check for overflow - sizes > MPU_MAX_ALIGNABLE_SIZE would violate round-up
   * semantics */
  if (size > MPU_MAX_ALIGNABLE_SIZE) {
    return 0; /* Signal error: cannot round up without overflow */
  }

  /* Find next power of 2 using efficient builtin (O(1) vs O(log n)) */
  if ((size & (size - 1)) == 0) {
    return size; /* Already power of 2 */
  }

  /* Round up to next power of 2 using portable count leading zeros
   * NOTE: size=1 case is safely handled by early returns above:
   * - size <= MPU_MIN_REGION_SIZE returns at line 54-56
   * - size=1 is power of 2, returns at line 64-66
   * Therefore size-1 >= 1 when reaching this point, avoiding CLZ(0) */
  uint32_t clz_result = CORTEX_M_CLZ_IMPL(size - 1);
  /* Ensure shift amount is valid (< 32) to prevent undefined behavior */
  if (clz_result >= 32) {
    return 0; /* Signal error: invalid clz result */
  }
  return 1U << (32 - clz_result);
}

/**
 * Check if a value is power of 2
 *
 * @param value Value to check
 * @return true if power of 2, false otherwise
 */
static inline pok_bool_t mpu_is_power_of_2(uint32_t value) {
  return (value != 0) && ((value & (value - 1)) == 0);
}

/**
 * Check if address is aligned to size with overflow protection
 *
 * @param addr Address to check
 * @param size Alignment size (must be power of 2)
 * @return TRUE if aligned, FALSE otherwise (including on invalid input)
 */
static inline pok_bool_t mpu_is_aligned(uint32_t addr, uint32_t size) {
  /* Protect against zero size and non-power-of-2 sizes */
  if (size == 0 || !mpu_is_power_of_2(size)) {
    return FALSE; /* Invalid size parameters */
  }

  /* Protect against size overflow when subtracting 1 */
  if (size > MPU_MAX_ALIGNABLE_SIZE) {
    return FALSE; /* Size too large for safe alignment check */
  }

  return (addr & (size - 1)) == 0;
}

/**
 * Safely align address up to next boundary with overflow protection
 *
 * @param addr Address to align
 * @param alignment Alignment boundary (must be power of 2)
 * @param out Pointer to store the aligned address
 * @return TRUE on success, FALSE on overflow/invalid input
 */
static inline pok_bool_t mpu_align_up(uint32_t addr, uint32_t alignment,
                                      uint32_t *out) {
  if (out == NULL)
    return FALSE;

  /* Validate alignment parameter */
  if (alignment == 0 || !mpu_is_power_of_2(alignment)) {
    return FALSE;
  }

  /* Check for potential overflow before calculation
   * We add (alignment - 1) to addr, so check if addr > UINT32_MAX - (alignment
   * - 1) */
  if (addr > (UINT32_MAX - (alignment - 1))) {
    return FALSE;
  }

  uint32_t mask = alignment - 1;
  uint32_t aligned = (addr + mask) & ~mask;

  /* Double-check no overflow occurred */
  if (aligned < addr)
    return FALSE;

  *out = aligned;
  return TRUE;
}

/**
 * Safely align address down to boundary
 *
 * @param addr Address to align
 * @param alignment Alignment boundary (must be power of 2)
 * @param out Pointer to store the aligned address
 * @return TRUE on success, FALSE on invalid input
 */
static inline pok_bool_t mpu_align_down(uint32_t addr, uint32_t alignment,
                                        uint32_t *out) {
  if (out == NULL)
    return FALSE;

  /* Validate alignment parameter */
  if (alignment == 0 || !mpu_is_power_of_2(alignment)) {
    return FALSE;
  }

  /* Safe to align down - no overflow possible */
  *out = addr & ~(alignment - 1);
  return TRUE;
}

#endif /* !__POK_ARM_MPU_UTILS_H__ */
