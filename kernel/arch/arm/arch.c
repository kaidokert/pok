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
 * \brief   Provides generic architecture interface for ARM Cortex-M architecture
 */

#include "mpu.h"
#include "nvic.h"
#include <core/partition.h>
#include <errno.h>
#include <arch.h>

extern pok_ret_t pok_arch_space_init(void);

pok_ret_t pok_arch_init() {
  pok_ret_t ret;
  
  pok_mpu_init();
  pok_nvic_init();
  
  ret = pok_arch_space_init();
  if (ret != POK_ERRNO_OK) {
    return ret;
  }

  return (POK_ERRNO_OK);
}

pok_ret_t pok_arch_preempt_disable() {
  __asm volatile ("cpsid i" : : : "memory");
  return (POK_ERRNO_OK);
}

pok_ret_t pok_arch_preempt_enable() {
  __asm volatile ("cpsie i" : : : "memory");
  return (POK_ERRNO_OK);
}

pok_ret_t pok_arch_idle() {
  while (1) {
    __asm volatile ("wfi");
  }
}

pok_ret_t pok_arch_event_register(uint8_t vector, void (*handler)(void)) {
  return pok_nvic_set_handler(vector, handler);
}

uint32_t pok_thread_stack_addr(const uint8_t partition_id,
                               const uint32_t local_thread_id) {
  return pok_partitions[partition_id].size - 8 -
         (local_thread_id * POK_USER_STACK_SIZE);
}

__attribute__((noreturn)) void pok_division_by_zero_error(void) {
  /* Force a division by zero to trigger HardFault */
  volatile int zero = 0;
  volatile int result = 42 / zero;
  (void)result;
  
  while (1) {
    __asm volatile ("wfi");
  }
}