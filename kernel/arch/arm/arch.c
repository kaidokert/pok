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
 * \file    arch/arm/arch.c
 * \author  POK team
 * \brief   Provides generic architecture interface for ARM Cortex-M
 * architecture
 */

/* POK system headers */
#include <errno.h>

/* POK core headers */
#include <arch.h>
#include <bsp.h>
#ifdef POK_NEEDS_DEPLOYMENT
#include <core/deployment.h>
#endif
#include <core/partition.h>
#include <core/thread.h>

/* Architecture-specific headers - conditional for full vs minimal build */
#ifdef POK_NEEDS_STM32F4_PERIPHERALS
#include "mpu.h"
#include "nvic.h"
#endif

/* Flexible constants - use deployment.h if available, otherwise defaults */
#ifndef POK_CONFIG_NB_PARTITIONS
#define POK_CONFIG_NB_PARTITIONS 2 /* Default for generic BSP */
#endif

#ifndef POK_USER_STACK_SIZE
#define POK_USER_STACK_SIZE                                                    \
  1024 /* Default stack size - small for embedded systems */
#endif

/* Stack address calculation constants */
#define POK_STACK_GUARD_BYTES 8     /* Guard offset for stack calculations */
#define POK_INVALID_STACK_ADDRESS 0 /* Invalid stack address return value */

extern pok_ret_t pok_arch_space_init(void);

pok_ret_t pok_arch_init(void) {
  pok_ret_t ret;

  ret = pok_mpu_init();
  if (ret != POK_ERRNO_OK) {
#if defined(POK_NEEDS_DEBUG) || defined(POK_NEEDS_CONSOLE)
    pok_cons_write("CRITICAL ERROR: pok_mpu_init() failed! Return code=", 51);
    char ret_str[4];
    ret_str[0] = '0' + (ret / 10);
    ret_str[1] = '0' + (ret % 10);
    ret_str[2] = '\n';
    ret_str[3] = '\0';
    pok_cons_write(ret_str, 3);
#endif
    return ret;
  }

  ret = pok_nvic_init();
#if defined(POK_NEEDS_DEBUG)
  pok_cons_write("pok_arch_init: pok_nvic_init returned ", 39);
  char nvic_ret[3];
  nvic_ret[0] = "0123456789ABCDEF"[(ret >> 4) & 0xF];
  nvic_ret[1] = "0123456789ABCDEF"[ret & 0xF];
  nvic_ret[2] = '\n';
  pok_cons_write(nvic_ret, 3);
#endif
  if (ret != POK_ERRNO_OK) {
    /* Cleanup: disable MPU on NVIC init failure */
    pok_mpu_disable();
    return ret;
  }

  ret = pok_arch_space_init();
#if defined(POK_NEEDS_DEBUG)
  pok_cons_write("pok_arch_init: pok_arch_space_init returned ", 44);
  char space_ret[3];
  space_ret[0] = "0123456789ABCDEF"[(ret >> 4) & 0xF];
  space_ret[1] = "0123456789ABCDEF"[ret & 0xF];
  space_ret[2] = '\n';
  pok_cons_write(space_ret, 3);
#endif
  if (ret != POK_ERRNO_OK) {
    /* Cleanup: disable MPU on space init failure */
    /* Note: NVIC cleanup not needed as it doesn't maintain state */
    pok_mpu_disable();
    return ret;
  }

  return POK_ERRNO_OK;
}

/**
 * PRIMASK helper functions for improved interrupt state management
 *
 * These functions provide safe access to the ARM Cortex-M PRIMASK register
 * which controls interrupt masking at the processor level.
 */

/**
 * Read current PRIMASK register value
 *
 * @return Current PRIMASK value (0 = interrupts enabled, 1 = disabled)
 */
static inline uint32_t pok_arch_primask_read(void) {
  uint32_t primask;
  __asm volatile("mrs %0, PRIMASK" : "=r"(primask)::"memory");
  return primask;
}

/**
 * Write value to PRIMASK register with proper synchronization
 *
 * @param primask PRIMASK value to write (0 = enable, 1 = disable interrupts)
 */
static inline void pok_arch_primask_write(uint32_t primask) {
  __asm volatile("msr PRIMASK, %0" ::"r"(primask) : "memory");
  __asm volatile(
      "isb" ::
          : "memory"); /* Ensure instruction sync after PRIMASK change */
}

/**
 * Disable interrupts while preserving previous interrupt state
 *
 * This function safely disables interrupts and optionally saves the previous
 * PRIMASK state for later restoration. Uses proper memory barriers to ensure
 * the interrupt disable takes effect before returning.
 *
 * @param prev_state Pointer to store previous interrupt state (can be NULL)
 * @return POK_ERRNO_OK on success
 */
pok_ret_t pok_arch_preempt_disable_save(uint32_t *prev_state) {
  uint32_t primask = pok_arch_primask_read();
  if (prev_state) {
    *prev_state = primask;
  }
  __asm volatile("cpsid i" ::: "memory"); /* Disable interrupts */
  __asm volatile("dsb" ::: "memory");     /* Data sync barrier */
  __asm volatile("isb" ::: "memory");     /* Instruction sync barrier */
  return POK_ERRNO_OK;
}

/**
 * Restore previous interrupt state
 *
 * Restores interrupts only if they were previously enabled, preventing
 * accidental enabling of interrupts that were already disabled.
 *
 * @param prev_state Previous interrupt state from pok_arch_preempt_disable_save
 * @return POK_ERRNO_OK on success
 */
pok_ret_t pok_arch_preempt_restore(uint32_t prev_state) {
  /* Only restore if previously enabled (PRIMASK bit 0 == 0) */
  if ((prev_state & 0x1u) == 0) {
    __asm volatile("cpsie i" ::: "memory"); /* Enable interrupts */
    __asm volatile("isb" ::: "memory");     /* Instruction sync barrier */
  }
  return POK_ERRNO_OK;
}

/* Legacy functions maintained for backward compatibility */
pok_ret_t pok_arch_preempt_disable() {
  __asm volatile("cpsid i" : : : "memory");
  __asm volatile("dsb" : : : "memory");
  __asm volatile("isb" : : : "memory");
  return POK_ERRNO_OK;
}

pok_ret_t pok_arch_preempt_enable() {
  __asm volatile("cpsie i" : : : "memory");
  __asm volatile("isb" : : : "memory");
  return POK_ERRNO_OK;
}

pok_ret_t pok_arch_idle(void) {
  while (1) {
    __asm volatile("wfi");
  }
  return POK_ERRNO_OK; // Never reached
}

pok_ret_t pok_arch_event_register(uint8_t vector, void (*handler)(void)) {
  return (pok_nvic_set_handler(vector, handler));
}

/**
 * Calculate stack address for a thread in a partition
 */
uint32_t pok_thread_stack_addr(const uint8_t partition_id,
                               const uint32_t local_thread_id) {
  /* W^X Security: Place stack in data region
   * With 8KB/8KB split (code/data), stacks start at partition_size and grow
   * down Data region starts at 0x2000, ensuring stack is in writable region
   * Thread stacks are 2KB each (reduced from 4KB for more threads):
   *   - Thread 0: 0x4000 - 2048 = 0x3800 (in data region)
   *   - Thread 1: 0x4000 - 40 - 2048 = 0x37D8 (in data region)
   *   - Thread 2: 0x4000 - 40 - 4096 = 0x2FD8 (in data region)
   *   - Thread 3: 0x4000 - 40 - 6144 = 0x27D8 (in data region)
   * This allows 4 threads per partition with W^X enforcement
   */
  uint32_t result = pok_partitions[partition_id].size - 40 -
                    (local_thread_id * POK_USER_STACK_SIZE);

  /* W^X bounds check: Ensure stack doesn't go below data region start (offset
   * 0x2000) */
  const uint32_t DATA_REGION_OFFSET = 0x2000; /* 8KB code region size */
  if (result < DATA_REGION_OFFSET) {
#ifdef POK_NEEDS_DEBUG
    printf(
        "ERROR: Thread %d stack would be in code region (offset 0x%x < 0x%x)\n",
        local_thread_id, result, DATA_REGION_OFFSET);
#endif
    return 0; /* Return invalid address to prevent thread creation */
  }
#ifdef POK_NEEDS_DEBUG
  printf("pok_thread_stack_addr: partition=%d, thread=%d, size=0x%x, "
         "result=0x%x\n",
         partition_id, local_thread_id, pok_partitions[partition_id].size,
         result);
#endif
  return result;
}

/**
 * Trigger a division by zero error for testing or error handling
 *
 * This function intentionally causes a division by zero to test UsageFault
 * handling when DIV_0_TRP is enabled in the SCB Configuration Control Register.
 * Used for testing exception handling or as a controlled failure mechanism.
 *
 * @note This function never returns
 */
__attribute__((noreturn)) void pok_division_by_zero_error(void) {
  /* Force a division by zero to trigger UsageFault (when DIV_0_TRP enabled) */
  volatile int zero = 0;
  volatile int dividend = 42;
  volatile int result;
  /* Prevent compiler optimization by using inline assembly to ensure division
   * occurs with actual modified values */
  /* ARCHITECTURAL DESIGN DECISION: Use portable C division operator
   * - ARMv7-M cores (Cortex-M3/M4/M7): Compiler generates 'sdiv' instruction
   * - ARMv6-M cores (Cortex-M0/M0+): Compiler generates software division
   * library call This ensures compatibility across all Cortex-M variants while
   * still triggering division-by-zero detection on cores with DIV_0_TRP
   * capability. */
  result = dividend / zero;
  (void)result;

  while (1) {
    __asm volatile("wfi");
  }
}
