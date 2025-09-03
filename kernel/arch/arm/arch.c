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
#include <stdint.h>

/* POK core headers */
#include <arch.h>
#include <core/partition.h>

/* Architecture-specific headers */
#include "mpu.h"
#include "nvic.h"

/* Stack address calculation constants */
#define POK_STACK_GUARD_BYTES 8     /* Guard offset for stack calculations */
#define POK_INVALID_STACK_ADDRESS 0 /* Invalid stack address return value */

extern pok_ret_t pok_arch_space_init(void);

pok_ret_t pok_arch_init(void) {
  pok_ret_t ret;

  ret = pok_mpu_init();
  if (ret != POK_ERRNO_OK) {
    return ret;
  }

  ret = pok_nvic_init();
  if (ret != POK_ERRNO_OK) {
    /* Cleanup: disable MPU on NVIC init failure */
    pok_mpu_disable();
    return ret;
  }

  ret = pok_arch_space_init();
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

pok_ret_t pok_arch_idle() {
  while (1) {
    __asm volatile("wfi");
  }
  /* This function never returns, but compiler needs a return statement */
  return POK_ERRNO_OK;
}

pok_ret_t pok_arch_event_register(uint8_t vector, void (*handler)(void)) {
  return (pok_nvic_set_handler(vector, handler));
}

/**
 * Calculate stack address for a thread in a partition
 *
 * This function performs comprehensive parameter validation and bounds checking
 * to ensure safe stack address calculation within partition memory boundaries.
 *
 * Parameter validation includes:
 * - partition_id must be < POK_CONFIG_NB_PARTITIONS
 * - local_thread_id must fit within partition size constraints
 * - Stack allocation must not exceed partition boundaries
 * - Uses 64-bit arithmetic to prevent overflow during calculations
 *
 * @param partition_id Partition ID (must be < POK_CONFIG_NB_PARTITIONS)
 * @param local_thread_id Local thread ID within partition
 * @return Stack address (8-byte aligned) or POK_INVALID_STACK_ADDRESS on error
 */
uint32_t pok_thread_stack_addr(const uint8_t partition_id,
                               const uint32_t local_thread_id) {
  /* Validate partition_id is within valid range */
  if (partition_id >= POK_CONFIG_NB_PARTITIONS) {
    return POK_INVALID_STACK_ADDRESS; /* Invalid partition ID */
  }

  /* Validate partition exists and has valid configuration */
  uint32_t partition_size = pok_partitions[partition_id].size;
  uint32_t partition_base = pok_partitions[partition_id].base_addr;

  /* Check for invalid partition configuration */
  if (partition_size == 0 || partition_base == 0) {
    return POK_INVALID_STACK_ADDRESS; /* Partition not properly initialized */
  }

  /* Check for partition_base + partition_size overflow */
  if (partition_base > (UINT32_MAX - partition_size)) {
    return POK_INVALID_STACK_ADDRESS; /* Partition end address overflows */
  }
  uint32_t partition_end = partition_base + partition_size;

  /* Validate partition size against minimum requirements */
  uint32_t effective_stack_size = POK_USER_STACK_SIZE + POK_STACK_GUARD_BYTES;
  if (partition_size < effective_stack_size) {
    return POK_INVALID_STACK_ADDRESS; /* Partition too small for even one thread
                                       */
  }

  /* Check for potential overflow in effective stack size calculation */
  if (POK_USER_STACK_SIZE > UINT32_MAX - POK_STACK_GUARD_BYTES) {
    return POK_INVALID_STACK_ADDRESS; /* Stack size configuration would overflow
                                       */
  }

  /* Calculate max threads with overflow protection */
  uint32_t max_threads = partition_size / effective_stack_size;
  if (max_threads == 0) {
    return POK_INVALID_STACK_ADDRESS; /* No threads can fit in partition */
  }

  /* Validate thread ID against calculated maximum */
  if (local_thread_id >= max_threads) {
    return POK_INVALID_STACK_ADDRESS; /* Thread ID too large for partition */
  }

  /* Use 64-bit arithmetic to prevent overflow - cast before multiplication */
  uint64_t stack_offset_64 =
      (uint64_t)local_thread_id * (uint64_t)effective_stack_size +
      POK_STACK_GUARD_BYTES;
  if (stack_offset_64 >= partition_size) {
    return POK_INVALID_STACK_ADDRESS; /* Stack offset exceeds partition size */
  }

  uint32_t stack_offset = (uint32_t)stack_offset_64;

  /* Validate that both stack start and end are within partition bounds */
  if ((stack_offset + POK_USER_STACK_SIZE) > partition_size) {
    return POK_INVALID_STACK_ADDRESS; /* Invalid stack address - stack extends
                                         beyond partition */
  }

  /* Calculate final stack address with additional validation */
  uint32_t stack_addr = partition_base + partition_size - stack_offset;

  /* Validate the calculated stack address is within partition bounds */
  if (stack_addr < partition_base || stack_addr >= partition_end) {
    return POK_INVALID_STACK_ADDRESS; /* Stack address calculation error */
  }

  /* Ensure 8-byte stack pointer alignment for ARM Cortex-M */
  uint32_t aligned_stack_addr = stack_addr & ~7;

  /* Final validation of aligned address */
  if (aligned_stack_addr < partition_base ||
      aligned_stack_addr >= partition_end) {
    return POK_INVALID_STACK_ADDRESS; /* Alignment caused address to go out of
                                         bounds */
  }

  /* Post-alignment bounds check: ensure both stack and guard space are
   * available For downward-growing stacks, verify space for full stack + guard
   * below aligned address */
  if (aligned_stack_addr <
      (partition_base + POK_USER_STACK_SIZE + POK_STACK_GUARD_BYTES)) {
    return POK_INVALID_STACK_ADDRESS; /* Insufficient space for stack + guard
                                         after alignment */
  }

  return aligned_stack_addr;
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
  /* Prevent compiler optimization by using inline assembly to ensure division
   * occurs */
  __asm volatile("" : "+r"(zero), "+r"(dividend));
  volatile int result = dividend / zero;
  (void)result;

  while (1) {
    __asm volatile("wfi");
  }
}
