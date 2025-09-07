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
#include <libc.h>

/* POK core headers */
#include <core/debug.h>
#include <core/partition.h>
#include <core/syscall.h>

/* Architecture-specific headers */
#include "arch.h"
#include "mpu.h"
#include "nvic.h"

/* External variables */
extern uint8_t pok_current_partition;

/* Extract partition ID from current MPU configuration */
static uint8_t pok_get_current_partition_id(void) {
  /* Get active user MPU region */
  uint8_t active_region = pok_mpu_get_active_user_region();

  /* Region 0 is kernel, user regions start at 1 */
  if (active_region == 0) {
    /* Running in kernel mode */
    return (pok_current_partition);
  }

  /* Convert region ID back to partition ID (partition_id = region_id - 1) */
  uint8_t partition_id = active_region - 1;

  /* Validate derived partition ID */
  if (partition_id >= POK_CONFIG_NB_PARTITIONS) {
    /* Fallback to global variable if derived ID is invalid */
    return (pok_current_partition);
  }

  return (partition_id);
}

/*
 * SVC Handler implementation - called by naked wrapper
 */
static void svc_handler_impl(uint32_t *frame) {
  pok_syscall_info_t syscall_info;
  pok_ret_t syscall_ret;
  pok_syscall_id_t syscall_id;

  if (frame == NULL) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: Invalid stack frame in SVC_Handler\n");
#endif
    return; /* Invalid stack frame */
  }

  /* Note: SVC number extraction removed - it required unsafe memory read
   * (PC-2 could fault) and is unused. POK uses a single SVC number (0) for
   * all system calls, with syscall type determined by register arguments.
   * If SVC number validation is needed in the future, it should be done
   * safely within the MPU-protected kernel region. */

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
   * r0 = syscall_id, r1 = syscall_args pointer (user virtual address)
   */
  syscall_id = (pok_syscall_id_t)frame[0]; /* r0 */

  /* Extract addressing info */
  uint32_t user_vaddr = frame[1]; /* r1 - user virtual address */
  uint32_t base_addr   = pok_partitions[syscall_info.partition].base_addr;
  uint32_t base_vaddr  = pok_partitions[syscall_info.partition].base_vaddr;
  uint32_t psize       = pok_partitions[syscall_info.partition].size;
  uint32_t args_size   = (uint32_t)sizeof(pok_syscall_args_t);

  /* Validate that the arguments pointer lies fully within the partition's
   * virtual range [base_vaddr, base_vaddr + psize) with overflow checks */
  if (args_size > psize) {
    syscall_ret = POK_ERRNO_EINVAL;
    goto syscall_exit;
  }
  if (base_vaddr > (0xFFFFFFFFu - psize)) {
    syscall_ret = POK_ERRNO_EINVAL; /* base_vaddr + size overflow */
    goto syscall_exit;
  }
  uint32_t part_vend = base_vaddr + psize;
  if (user_vaddr < base_vaddr || user_vaddr > (part_vend - args_size)) {
    syscall_ret = POK_ERRNO_EINVAL; /* Pointer outside partition vaddr range */
    goto syscall_exit;
  }
  /* Check for underflow in offset calculation */
  if (base_vaddr > base_addr) {
    syscall_ret = POK_ERRNO_EINVAL; /* Invalid partition configuration */
    goto syscall_exit;
  }

  uint32_t kernel_offset = base_addr - base_vaddr;

  /* Check for 32-bit pointer addition overflow using safer arithmetic */
  if (user_vaddr > (0xFFFFFFFFU - kernel_offset)) {
    syscall_ret = POK_ERRNO_EINVAL; /* Address overflow */
    goto syscall_exit;
  }

  uint32_t kernel_addr = user_vaddr + kernel_offset;

  /* Ensure kernel address meets args struct alignment */
  const size_t args_align = __alignof__(pok_syscall_args_t);
  if (args_align && (kernel_addr & (args_align - 1))) {
    syscall_ret = POK_ERRNO_EINVAL; /* Misaligned address */
    goto syscall_exit;
  }
  /*
   * SECURITY: Copy syscall arguments to kernel-owned buffer to prevent TOCTOU
   * attacks. A malicious partition could modify arguments after validation but
   * before use, potentially corrupting kernel processing or escalating
   * privileges.
   */
  pok_syscall_args_t kernel_args_copy;

  /* Atomic copy from user space to kernel buffer to prevent concurrent
   * modification */
  memcpy(&kernel_args_copy, (void *)(uintptr_t)kernel_addr,
         sizeof(pok_syscall_args_t));

  /*
   * Execute the system call using the safe kernel copy
   */
  syscall_ret = pok_core_syscall(syscall_id, &kernel_args_copy, &syscall_info);

syscall_exit:
  /*
   * Return the result in r0
   */
  frame[0] = (uint32_t)syscall_ret;

  /* Memory barriers before returning to thread mode to ensure all kernel
   * memory operations complete and instructions are synchronized before
   * exception return */
  __asm volatile("dsb" ::: "memory");
  __asm volatile("isb" ::: "memory");
}

/*
 * SVC Handler - naked wrapper that determines stack pointer and calls
 * implementation
 */
void __attribute__((naked)) SVC_Handler(void) {
  __asm volatile(
      /* Determine which stack pointer to use based on EXC_RETURN */
      "tst lr, #4                 \n" /* Test EXC_RETURN[2] for stack pointer */
      "ite eq                     \n"
      "mrseq r0, msp              \n" /* If from MSP, use MSP */
      "mrsne r0, psp              \n" /* If from PSP, use PSP */

      /* Call the implementation with frame pointer in r0 */
      "push {lr}                  \n" /* Save EXC_RETURN */
      "bl svc_handler_impl        \n"
      "pop {lr}                   \n" /* Restore EXC_RETURN */
      "bx lr                      \n" /* Return from exception */
      :
      :
      : "r0", "memory");
}

/*
 * PendSV Handler - handles context switches
 * This is called when pok_context_switch() triggers the PendSV exception
 *
 * NOTE: FPU context not saved since build uses -mfloat-abi=soft
 * All floating point operations are handled by software libraries
 */
void __attribute__((naked)) PendSV_Handler(void) {
  extern uint32_t *g_old_sp_ptr;
  extern uint32_t g_new_sp;

  __asm volatile(
      /* Check if we are returning to thread mode using PSP. If not, we came
         from MSP (kernel) and should not save context */
      "tst lr, #4                 \n" /* Test EXC_RETURN[2] for stack pointer */
      "beq 1f                     \n" /* If from MSP, skip saving context */

      /* Save context from PSP */
      "mrs r0, psp                \n" /* Get current Process Stack Pointer */
      "stmdb r0!, {r4-r11}        \n" /* Save r4-r11 (callee-saved regs) */
      "ldr r1, =g_old_sp_ptr      \n" /* Load address of g_old_sp_ptr */
      "ldr r1, [r1]               \n" /* Load g_old_sp_ptr value (the address of
                                         sp) */
      "cbz r1, 1f                 \n" /* Skip if NULL */
      "str r0, [r1]               \n" /* Store new PSP value into the thread
                                         struct */

      "1:                         \n" /* Load new thread context */
      "ldr r2, =g_new_sp          \n" /* r2 = &g_new_sp */
      "ldr r0, [r2]               \n" /* r0 = g_new_sp value */
      "cbz r0, 3f                 \n" /* Skip if NULL */

      "ldmia r0!, {r4-r11}        \n" /* Restore r4-r11 from new thread's stack
                                       */
      "msr psp, r0                \n" /* Set new Process Stack Pointer */

      /* Clear g_new_sp to prevent stale reuse */
      "movs r3, #0                \n"
      "str r3, [r2]               \n"

      /* Ensure memory ops complete before return */
      "dsb                        \n"
      "isb                        \n"

      "3:                         \n"
      /* Return with original EXC_RETURN value preserved in LR */
      "bx lr                      \n" /* Return from exception */

      :
      :
      : "r0", "r1", "r2", "r3", "memory");
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
  pok_ret_t ret;

  /* SVC handler is already set in vector table */
  /* Set up PendSV for context switching */
  ret = pok_nvic_set_handler(EXCEPTION_PENDSV, PendSV_Handler);
  if (ret != POK_ERRNO_OK) {
    return ret;
  }

  ret = pok_nvic_set_priority(EXCEPTION_PENDSV, NVIC_PRIORITY_LOWEST);
  if (ret != POK_ERRNO_OK) {
    return ret;
  }

  return POK_ERRNO_OK;
}
