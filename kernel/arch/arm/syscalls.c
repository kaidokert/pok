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

/* POK system headers */
#include <errno.h>

/* POK core headers */
#include <core/debug.h>
#include <core/partition.h>
#include <core/syscall.h>

/* Architecture-specific headers */
#include "arch.h"
#include "mpu.h"
#include "nvic.h"

/* Extract partition ID from current MPU configuration */
static uint8_t pok_get_current_partition_id(void) {
  /* Get active user MPU region */
  uint8_t active_region = pok_mpu_get_active_user_region();
  
  /* Region 0 is kernel, user regions start at 1 */
  if (active_region == 0) {
    /* Running in kernel mode */
    extern uint8_t pok_current_partition;
    return (pok_current_partition);
  }
  
  /* Convert region ID back to partition ID (partition_id = region_id - 1) */
  uint8_t partition_id = active_region - 1;
  
  /* Validate derived partition ID */
  if (partition_id >= POK_CONFIG_NB_PARTITIONS) {
    /* Fallback to global variable if derived ID is invalid */
    extern uint8_t pok_current_partition;
    return (pok_current_partition);
  }
  
  return (partition_id);
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
  __asm volatile("mrs %0, psp" : "=r"(frame));

  if (frame == NULL) {
    return; /* Invalid stack frame */
  }

  /* Extract SVC number from the SVC instruction */
  uint16_t *svc_addr =
      (uint16_t *)(frame[6] - 2);       /* PC points to instruction after SVC */
  uint16_t svc_instruction = *svc_addr; /* Read the 16-bit SVC instruction */
  uint8_t svc_number =
      svc_instruction & ARM_SVC_NUMBER_MASK; /* SVC number is in lower 8 bits */
  (void)svc_number; /* Currently unused - could be used for SVC routing */

  /*
   * Set up syscall information
   */
  syscall_info.partition = pok_get_current_partition_id();

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
  syscall_id = (pok_syscall_id_t)frame[0]; /* r0 */
  syscall_args =
      (pok_syscall_args_t *)(frame[1]); /* r1 */
  /*
   * Validate that the arguments pointer is within partition bounds
   */
  if (!pok_check_ptr_in_partition(syscall_info.partition, (void *)frame[1],
                                  sizeof(pok_syscall_args_t))) {
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
 * This is called when pok_context_switch() triggers the PendSV exception
 */
void __attribute__((naked)) PendSV_Handler(void) {
  /* Access global variables from thread.c */
  extern uint32_t *g_old_sp_ptr;
  extern uint32_t g_new_sp;

  __asm volatile(
      /* Save current thread context */
      "mrs r0, psp                \n" /* Get current Process Stack Pointer */
      "cbz r0, 1f                 \n" /* Skip if PSP is NULL (first time) */

      "stmdb r0!, {r4-r11}        \n" /* Save r4-r11 (callee-saved regs) */

      /* Store updated PSP to old thread's stack pointer */
      "ldr r1, =g_old_sp_ptr      \n" /* Load address of g_old_sp_ptr */
      "ldr r1, [r1]               \n" /* Load g_old_sp_ptr value */
      "cbz r1, 1f                 \n" /* Skip if NULL */
      "str r0, [r1]               \n" /* Store new PSP value */

      "1:                         \n" /* Load new thread context */
      "ldr r0, =g_new_sp          \n" /* Load address of g_new_sp */
      "ldr r0, [r0]               \n" /* Load g_new_sp value */
      "cbz r0, 2f                 \n" /* Skip if NULL */

      "ldmia r0!, {r4-r11}        \n" /* Restore r4-r11 from new thread's stack
                                       */
      "msr psp, r0                \n" /* Set new Process Stack Pointer */

      "2:                         \n"
      /* Ensure thread mode with PSP */
      "ldr r0, =0xFFFFFFFD        \n" /* EXC_RETURN: Return to Thread, use PSP
                                       */
      "bx r0                      \n" /* Return from exception */

      ::
          : "memory");
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

  return POK_ERRNO_OK;
}
