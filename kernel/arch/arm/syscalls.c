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
 * \file kernel/arch/arm/syscalls.c
 * \brief ARM Cortex-M system call implementation using SVC
 * \author POK team
 */

#include <core/debug.h>
#include <core/partition.h>
#include <core/syscall.h>
#include <errno.h>

#include "nvic.h"
#include "mpu.h"

/* Extract partition ID from current MPU configuration */
static uint8_t get_current_partition_id(void) {
  /* In this simple implementation, we track the current partition */
  /* This could be enhanced to use MPU region information */
  extern uint8_t pok_current_partition;
  return pok_current_partition;
}

/*
 * SVC Handler - handles system calls
 * The SVC number and arguments are passed via registers
 */
void SVC_Handler(void) {
  uint32_t *frame;
  pok_syscall_info_t syscall_info;
  pok_ret_t syscall_ret;
  pok_syscall_args_t *syscall_args;
  pok_syscall_id_t syscall_id;
  
  /* Get the stack frame from PSP */
  __asm volatile ("mrs %0, psp" : "=r" (frame));
  
  /* Extract SVC number from the SVC instruction */
  uint16_t *svc_addr = (uint16_t *)(frame[6] - 2); /* PC points to instruction after SVC */
  uint16_t svc_instruction = *svc_addr;             /* Read the 16-bit SVC instruction */
  uint8_t svc_number = svc_instruction & 0xFF;     /* SVC number is in lower 8 bits */
  
  /*
   * Set up syscall information
   */
  syscall_info.partition = get_current_partition_id();
  
  if (syscall_info.partition >= POK_CONFIG_NB_PARTITIONS) {
    syscall_ret = POK_ERRNO_EINVAL;
    goto syscall_exit;
  }
  
  syscall_info.base_addr = pok_partitions[syscall_info.partition].base_addr;
  syscall_info.thread = POK_SCHED_CURRENT_THREAD;
  
  /* 
   * Get syscall arguments from registers
   * r0 = syscall_id, r1 = syscall_args pointer
   */
  syscall_id = (pok_syscall_id_t)frame[0];  /* r0 */
  syscall_args = (pok_syscall_args_t *)(frame[1] + syscall_info.base_addr);  /* r1 */
  
  /*
   * Validate that the arguments pointer is within partition bounds
   */
  if (pok_check_ptr_in_partition(syscall_info.partition, (void *)frame[1],
                                 sizeof(pok_syscall_args_t)) == 0) {
    syscall_ret = POK_ERRNO_EINVAL;
    goto syscall_exit;
  }
  
  /*
   * Execute the system call
   */
  syscall_ret = pok_core_syscall(syscall_id, syscall_args, &syscall_info);
  
syscall_exit:
  /*
   * Return the result in r0
   */
  frame[0] = (uint32_t)syscall_ret;
}

/*
 * PendSV Handler - handles context switches
 */
void PendSV_Handler(void) {
  /* Context switching is handled by the scheduler */
  /* This handler completes the context switch initiated by pok_context_switch */
  
  __asm volatile (
    /* Context switch is already prepared by pok_context_switch */
    /* Just return to continue with new context */
    "bx lr"
  );
}

/*
 * SysTick Handler - system timer
 */
void SysTick_Handler(void) {
  /* Forward to generic POK timer handler */
  extern void pok_timer_handler(void);
  pok_timer_handler();
}

/**
 * Initialize system call handling
 */
pok_ret_t pok_syscall_init(void) {
  /* SVC handler is already set in vector table */
  /* Set up PendSV for context switching */
  pok_nvic_set_handler(EXCEPTION_PENDSV, PendSV_Handler);
  pok_nvic_set_priority(EXCEPTION_PENDSV, NVIC_PRIORITY_LOWEST);
  
  return (POK_ERRNO_OK);
}