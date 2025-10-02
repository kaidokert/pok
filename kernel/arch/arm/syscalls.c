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
#include <bsp.h>
#include <core/debug.h>
#include <core/partition.h>
#include <core/syscall.h>

/* Constants for context switching */
#define CORTEX_M_SOFTWARE_FRAME_SIZE                                           \
  32 /* Size of r4-r11 saved by software (8 * 4 bytes) */

/* No forward declarations needed - using common pok_core_syscall() */

/* Architecture-specific headers */
#include "arch.h"
#include "mpu.h"
#include "nvic.h"

/* External variables */
extern uint8_t pok_current_partition;

/* Forward declarations */
/* static uint8_t pok_get_current_partition_id(void); - UNUSED */
static void svc_handler_impl(uint32_t *frame);

/* External syscall function */
extern pok_ret_t pok_core_syscall(const pok_syscall_id_t syscall_id,
                                  const pok_syscall_args_t *args,
                                  const pok_syscall_info_t *infos);

/**
 * Safe copy from user space with fault protection
 *
 * Performs byte-by-byte copying with bounds checking to avoid hard faults
 * on unmapped or access-violating addresses.
 *
 * @param dest Destination buffer (kernel space)
 * @param src Source buffer (user space, already validated to be in partition)
 * @param size Number of bytes to copy
 * @return POK_ERRNO_OK on success, POK_ERRNO_EINVAL on fault
 */
#if 0 /* UNUSED FUNCTION - commented out */
static pok_ret_t pok_safe_copy_from_user(void *dest, const void *src,
                                         size_t size) {
  if (dest == NULL || src == NULL || size == 0) {
    return POK_ERRNO_EINVAL;
  }

  /* Best-effort range prevalidation against current partition virtual bounds to
   * avoid mid-copy faults */
  uint8_t part = pok_get_current_partition_id();
  if (part >= POK_CONFIG_NB_PARTITIONS) {
    return POK_ERRNO_EINVAL;
  }
  /* STUB: Use simple address validation without partition table */
  uint32_t base_vaddr = 0x20000000; /* Typical ARM user space start */
  uint32_t psize = 0x10000;        /* 64KB partition size */
  uint32_t vend;
  if (psize == 0 || base_vaddr > 0xFFFFFFFFu - psize) {
    return POK_ERRNO_EINVAL;
  }
  vend = base_vaddr + psize;

  uintptr_t u = (uintptr_t)src;
  if (u < base_vaddr || u > vend - 1) {
    return POK_ERRNO_EINVAL;
  }
  if (size > (vend - u)) {
    return POK_ERRNO_EINVAL;
  }

  const uint8_t *src_bytes = (const uint8_t *)src;
  uint8_t *dest_bytes = (uint8_t *)dest;
  for (size_t i = 0; i < size; i++) {
    volatile const uint8_t *src_ptr = &src_bytes[i];
    dest_bytes[i] = *src_ptr;
  }
  return POK_ERRNO_OK;
}
#endif

#if 0 /* UNUSED - commented out */
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
#endif

/*
 * SVC Handler implementation - called by naked wrapper
 */
static void __attribute__((used)) svc_handler_impl(uint32_t *frame) {
  pok_ret_t syscall_ret;
  pok_syscall_id_t syscall_id;

  if (frame == NULL) {
#ifdef POK_NEEDS_DEBUG
    /* ERROR: Invalid stack frame in SVC_Handler */
#endif
    return; /* Invalid stack frame */
  }

  /* ARM SYSCALL HANDLING WITH PROPER SYSCALL INFO */
  pok_syscall_info_t syscall_info;

  /* Get syscall arguments from registers */
  syscall_id = (pok_syscall_id_t)frame[0]; /* r0 */

  /* Populate syscall info structure like x86 does */
  syscall_info.partition = POK_SCHED_CURRENT_PARTITION;
  /* ARM partitions are position-dependent (linked at absolute addresses),
   * so no base_addr offset is needed - pointers are already correct */
  syscall_info.base_addr = 0;
  syscall_info.thread = POK_SCHED_CURRENT_THREAD;

  /* Use args pointer directly - syscall implementation handles base address
   * conversion */
  pok_syscall_args_t *args = (pok_syscall_args_t *)frame[1];

  /* Call core syscall handler with proper info */
  syscall_ret = pok_core_syscall(syscall_id, args, &syscall_info);

  /* Return the result in r0 */
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
void __attribute__((naked, no_instrument_function)) SVC_Handler(void) {
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
void __attribute__((naked, no_instrument_function)) PendSV_Handler(void) {
  extern uint32_t *g_old_sp_ptr;
  extern uint32_t g_new_sp;

  /* Mark variables as used to suppress warnings (they're used in asm) */
  (void)g_old_sp_ptr;
  (void)g_new_sp;

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
      /* CRITICAL FIX: Store PSP pointing to hardware frame, not after software
         regs */
      "add r3, r0, #32            \n" /* r3 = r0 + CORTEX_M_SOFTWARE_FRAME_SIZE
                                       */
      "str r3, [r1]               \n" /* Store corrected PSP value into thread
                                         struct */

      "1:                         \n" /* Load new thread context */
      "ldr r2, =g_new_sp          \n" /* r2 = &g_new_sp */
      "ldr r0, [r2]               \n" /* r0 = g_new_sp value (points to hardware
                                         frame) */
      "cbz r0, 3f                 \n" /* Skip if NULL - no context switch */

      /* CRITICAL FIX: Adjust PSP to point to software-saved registers for
         restoration */
      "sub r0, r0, #32            \n" /* r0 = r0 - CORTEX_M_SOFTWARE_FRAME_SIZE
                                       */
      "ldmia r0!, {r4-r11}        \n" /* Restore r4-r11, r0 now points to
                                             hardware frame */
      "msr psp, r0                \n" /* Set PSP to hardware frame for
                                         exception return */

      /* Clear g_new_sp to prevent stale reuse */
      "movs r3, #0                \n"
      "str r3, [r2]               \n"

      /* Ensure memory ops complete before return */
      "dsb                        \n"
      "isb                        \n"

      /* Check if new SP is in partition memory (RAM 0x20010000+) */
      /* Kernel threads use high RAM (0x2001F000+), partition threads use low
         RAM */
      "ldr r1, =0x20018000        \n" /* Threshold: above this is kernel */
      "cmp r0, r1                 \n"
      "bhs 4f                     \n" /* If SP >= threshold, kernel thread */

      /* Partition thread: Force EXC_RETURN to Thread mode using PSP
         (0xFFFFFFFD) */
      "ldr lr, =0xFFFFFFFD        \n" /* EXC_RETURN: Thread mode, use PSP */
      "bx lr                      \n" /* Return from exception */

      "4:                         \n"
      /* Kernel/idle thread: Keep Handler mode with MSP */
      "bx lr                      \n" /* Return with original LR (Handler mode)
                                       */

      "3:                         \n"
      /* No context switch - return with original LR (kernel mode) */
      "bx lr                      \n" /* Return from exception */

      :
      :
      : "r0", "r1", "r2", "r3", "memory");
}

/*
 * SysTick Handler - system timer
 * Note: SysTick_Handler is now implemented in startup.S
 * for proper ARM interrupt handling with register save/restore
 */

/* ARM now uses the common pok_core_syscall() implementation */

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

  ret = pok_nvic_set_priority(EXCEPTION_PENDSV, 15);
  if (ret != POK_ERRNO_OK) {
    return ret;
  }

  return POK_ERRNO_OK;
}
