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

/**
 * \file    arch/arm/stm32f4/pm.c
 * \author  POK team
 * \brief   ARM STM32F4 Physical Memory Management (like x86)
 */

#include <errno.h>
#include <libc.h>
#include <types.h>

#include "pm.h"

extern void *__pok_end;

uint32_t pok_arm_pm_heap_start;
uint32_t pok_arm_pm_brk;
uint32_t pok_arm_pm_heap_end;

int pok_pm_init() {
#ifdef POK_NEEDS_DEBUG
  printf("pok_pm_init: Initializing heap variables\n");
#endif

  // Use BSP-defined user memory region for heap
  // This ensures alignment with BSP memory allocation expectations
  /* Heap must be aligned for MPU power-of-2 regions
   * Use 32KB alignment to support 32KB partitions
   * With 128KB SRAM: 32KB kernel + 96KB heap (3x 32KB partitions) */
  pok_arm_pm_heap_start = pok_arm_pm_brk = 0x20008000; /* 32KB aligned */
  pok_arm_pm_heap_end = 0x20020000; /* End of 128KB SRAM gives 96KB heap */

#ifdef POK_NEEDS_DEBUG
  printf("pok_pm_init: heap_start=0x%x, brk=0x%x, heap_end=0x%x\n",
         pok_arm_pm_heap_start, pok_arm_pm_brk, pok_arm_pm_heap_end);
#endif

  return (POK_ERRNO_OK);
}

/**
 * Simple sbrk implementation for ARM with boundary checking
 */
uint32_t pok_pm_sbrk(uint32_t increment) {
  uint32_t addr;

/* ARM MPU requires partition base addresses to be aligned
 * Using 4KB (0x1000) alignment for partition boundaries */
#define PARTITION_ALIGNMENT 0x1000

  /* Align current brk to partition boundary before allocation */
  uint32_t aligned_brk =
      (pok_arm_pm_brk + PARTITION_ALIGNMENT - 1) & ~(PARTITION_ALIGNMENT - 1);

#ifdef POK_NEEDS_DEBUG
  printf("pok_pm_sbrk: increment=0x%x, brk=0x%x, aligned=0x%x, end=0x%x\n",
         increment, pok_arm_pm_brk, aligned_brk, pok_arm_pm_heap_end);
#endif

  // Check if allocation would exceed heap bounds (using aligned address)
  if (aligned_brk + increment > pok_arm_pm_heap_end) {
#ifdef POK_NEEDS_DEBUG
    printf("pok_pm_sbrk: FAILED - would exceed heap bounds\n");
#endif
    return (0); // Return NULL for out-of-memory
  }

  addr = aligned_brk;
  pok_arm_pm_brk = aligned_brk + increment;

#ifdef POK_NEEDS_DEBUG
  printf("pok_pm_sbrk: SUCCESS - allocated at 0x%x, new_brk=0x%x\n", addr,
         pok_arm_pm_brk);
#endif

  return (addr);
}
