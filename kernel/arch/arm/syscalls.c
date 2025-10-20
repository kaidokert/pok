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

/* Memory layout: Partition stacks use lower RAM, kernel/idle use upper RAM
 * This threshold separates them for determining EXC_RETURN mode in PendSV */
#define KERNEL_STACK_THRESHOLD 0x20018000u

/* No forward declarations needed - using common pok_core_syscall() */

/* Architecture-specific headers */
#include "arch.h"
#include "mpu.h"
#include "nvic.h"

/* External variables */
extern uint8_t pok_current_partition;

/* Constant for inline assembly access - marked 'used' for assembly reference */
static const uint32_t __attribute__((used)) kernel_stack_threshold =
    KERNEL_STACK_THRESHOLD;

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

/* Debug function to check thread 1 frame before restore */
void __attribute__((used)) debug_check_thread1_frame(uint32_t sp_value) {
  (void)sp_value; /* Suppress unused warning when POK_NEEDS_DEBUG is not defined
                   */
#ifdef POK_NEEDS_DEBUG
  printf("\n=== PENDSV: About to restore thread 1 ===\n");
  printf("SP value from TCB: 0x%x\n", sp_value);
  printf("SW frame addr (SP-32): 0x%x\n", sp_value - 32);

  volatile uint32_t *sw_frame = (volatile uint32_t *)(sp_value - 32);
  volatile uint32_t *hw_frame = (volatile uint32_t *)sp_value;

  printf("SW frame [r4-r11]:");
  for (int i = 0; i < 8; i++) {
    printf(" %x", sw_frame[i]);
  }
  printf("\nHW frame [r0-xpsr]:");
  for (int i = 0; i < 8; i++) {
    printf(" %x", hw_frame[i]);
  }
  printf("\n");
  printf("Expected PC: 0x200100F5, Actual PC: 0x%x\n", hw_frame[6]);
  printf("=====================================\n");
#endif
}

/* Debug function to log values loaded by PendSV */
void __attribute__((used, noinline)) debug_pendsv_load(uint32_t loaded_sp) {
#ifdef POK_NEEDS_DEBUG
  extern uint32_t g_next_sp_value;
  printf("PENDSV_LOAD: g_next_sp_value=%x loaded_sp=%x\n", g_next_sp_value,
         loaded_sp);
#else
  (void)loaded_sp;
#endif
}

/* Debug variables for PendSV - written by asm, read after fault */
volatile uint32_t __attribute__((used)) pendsv_debug_flag_value = 0xDEADBEEF;
volatile uint32_t __attribute__((used)) pendsv_debug_r0_after_load = 0xDEADBEEF;
volatile uint32_t __attribute__((used)) pendsv_debug_r2_addr = 0xDEADBEEF;
volatile uint32_t __attribute__((used)) pendsv_debug_psp_after_set = 0xDEADBEEF;

/* Flag to track if timer has been enabled after first context switch */
volatile uint8_t __attribute__((used)) pendsv_timer_enabled = 0;

/*
 * SVC Handler implementation - called by naked wrapper
 * CRITICAL: noinline prevents compiler from optimizing away frame accesses
 */
static void __attribute__((used, noinline, noclone))
svc_handler_impl(uint32_t *frame) {
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

  /* Get syscall arguments from registers
   * Use volatile to prevent compiler from caching or reordering these reads */
  volatile uint32_t *vframe = (volatile uint32_t *)frame;

  /* DEBUG: Read all 8 words from frame before asserting */
  uint32_t frame_r0 = vframe[0];
  (void)frame_r0; /* May be used in debug output below */

  syscall_id = (pok_syscall_id_t)vframe[0]; /* r0 */

  /* Declare args pointer early so it can be used in debug code */
  pok_syscall_args_t *args = (pok_syscall_args_t *)vframe[1];

  /* Memory barrier to ensure frame read completes before use */
  __asm volatile("" ::: "memory");

  /* CRITICAL ASSERTION: Syscall ID must never be 0 - indicates register
   * corruption */
  if (syscall_id == 0) {
#ifdef POK_NEEDS_DEBUG
    /* Add extra debug to understand what's happening */
    uint32_t actual_psp;
    __asm volatile("mrs %0, psp" : "=r"(actual_psp));

    printf("\n!!! SYSCALL CORRUPTION DEBUG !!!\n");
    printf("frame ptr = 0x%x, PSP = 0x%x\n", (uint32_t)frame, actual_psp);

    /* Print entire hardware frame */
    printf("HW frame [r0-xpsr]:");
    for (int i = 0; i < 8; i++) {
      printf(" 0x%x", vframe[i]);
    }
    printf("\n");

    /* Print detailed breakdown */
    printf("  r0 = 0x%x (syscall_id - CORRUPT!)\n", vframe[0]);
    printf("  r1 = 0x%x (args ptr)\n", vframe[1]);
    printf("  r2 = 0x%x\n", vframe[2]);
    printf("  r3 = 0x%x\n", vframe[3]);
    printf("  r12 = 0x%x\n", vframe[4]);
    printf("  LR = 0x%x\n", vframe[5]);
    printf("  PC = 0x%x\n", vframe[6]);
    printf("  xPSR = 0x%x\n", vframe[7]);
#endif
  }

#if 0
#ifdef POK_NEEDS_DEBUG
  printf("SVC: id=%d thr=%d PC=0x%x r0=0x%x\n", syscall_id, POK_SCHED_CURRENT_THREAD, vframe[6], vframe[0]);
#endif
#endif

  /* Populate syscall info structure like x86 does */
  syscall_info.partition = POK_SCHED_CURRENT_PARTITION;
  /* ARM partitions are position-dependent (linked at absolute addresses),
   * so no base_addr offset is needed - pointers are already correct */
  syscall_info.base_addr = 0;
  syscall_info.thread = POK_SCHED_CURRENT_THREAD;

  /* Call core syscall handler with proper info */
  syscall_ret = pok_core_syscall(syscall_id, args, &syscall_info);

  /* Return the result in r0 - write to volatile frame */
  vframe[0] = (uint32_t)syscall_ret;

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

      /* Memory barrier to ensure syscall writes are visible */
      "dsb                        \n"
      "isb                        \n"

      /* Check if context switch is needed (syscall may have triggered one) */
      "ldr r0, =g_reschedule_needed \n"
      "ldrb r1, [r0]              \n"
      "cbz r1, svc_no_switch      \n" /* If no switch needed, return normally */

      /* Context switch needed - perform it directly in SVC handler */
      /* This avoids PendSV and tail-chaining complexities */

      /* Check if we need to save current thread */
      "ldr r1, =g_current_sp_ptr  \n"
      "ldr r1, [r1]               \n" /* Load pointer to current thread's SP */
      "cbz r1, svc_load_next      \n" /* Skip save if NULL */

      /* Save current thread context */
      "mrs r0, psp                \n" /* Get PSP (points to HW frame) */
      "stmdb r0!, {r4-r11}        \n" /* Save SW frame below HW frame */
      "str r0, [r1]               \n" /* Store SW base to TCB */
      "dsb                        \n" /* Ensure write completes */

      "svc_load_next:             \n"
      /* Load next thread context */
      "ldr r2, =g_next_sp_value   \n"
      "ldr r0, [r2]               \n" /* r0 = next thread's SW base */
      "cbz r0, svc_no_switch      \n" /* Skip if NULL */

      "ldmia r0!, {r4-r11}        \n" /* Restore SW frame, r0 now points to HW
                                       */
      "mov r3, r0                 \n" /* Save r0 (expected PSP) */
      "msr psp, r0                \n" /* Set PSP to HW frame */
      "dsb                        \n" /* Ensure PSP write completes */
      "isb                        \n"

      /* DEBUG: PSP verification removed to avoid linker errors */

      /* Clear reschedule flag FIRST, then clear PendSV */
      "ldr r1, =g_reschedule_needed \n"
      "movs r2, #0                \n"
      "strb r2, [r1]              \n"
      "dsb                        \n" /* Ensure flag clear is visible */

      /* Clear PendSV pending bit since we handled the switch here */
      "ldr r1, =0xE000ED04        \n" /* SCB_ICSR address */
      "ldr r2, =0x08000000        \n" /* PENDSVCLR bit (bit 27) */
      "str r2, [r1]               \n"
      "dsb                        \n" /* Ensure ICSR write completes */
      "isb                        \n" /* Synchronize */

      /* Force return to thread mode using PSP */
      "ldr lr, =0xFFFFFFFD        \n"

      "svc_no_switch:             \n"
      "bx lr                      \n" /* Return from exception */
      :
      :
      : "r0", "r1", "r2", "memory");
}

/*
 * PendSV Handler - handles context switches
 * This is called when pok_context_switch() triggers the PendSV exception
 *
 * NOTE: FPU context not saved since build uses -mfloat-abi=soft
 * All floating point operations are handled by software libraries
 */
void __attribute__((naked, no_instrument_function)) PendSV_Handler(void) {
  extern uint8_t g_reschedule_needed;
  extern uint32_t *g_current_sp_ptr;
  extern uint32_t g_next_sp_value;

  /* Mark variables as used to suppress warnings (they're used in asm) */
  (void)g_reschedule_needed;
  (void)g_current_sp_ptr;
  (void)g_next_sp_value;

  __asm volatile(
      /* IDEMPOTENT RESCHEDULE PATTERN:
         Check if a reschedule is actually needed. If not, just return.
         This makes PendSV safe to call multiple times. */
      "ldr r0, =g_reschedule_needed \n" /* Load address of flag */
      "ldrb r1, [r0]              \n"   /* Load flag value */

      /* DEBUG: Store flag value to global */
      "ldr r3, =pendsv_debug_flag_value \n"
      "str r1, [r3]               \n"

      "cbz r1, 3f                 \n" /* If not set, return immediately */

      /* Context switch is needed - proceed with save/restore */

      /* CRITICAL INSIGHT: Whether PendSV is called from thread mode or handler
       * mode, the thread's r4-r11 values are ALWAYS in the CPU registers when
       * we get here:
       * - From thread mode (PSP): Thread was interrupted, r4-r11 not touched by
       * hardware
       * - From handler mode (MSP): SVC preserved r4-r11 per calling convention
       * In BOTH cases, we need to save r4-r11 from registers to the thread's
       * stack.
       */

      /* Check if we need to save current thread context */
      "ldr r1, =g_current_sp_ptr  \n" /* Load address of pointer to current
                                         thread's SP field */

      /* DEBUG: g_current_sp_ptr storage removed to avoid linker errors */

      "ldr r1, [r1]               \n" /* Load the pointer itself */
      "cbz r1, 1f                 \n" /* Skip if NULL - no thread to save */

      /* CRITICAL FIX: Always save PSP context if g_current_sp_ptr is set.
       * Previous code checked EXC_RETURN, but when PendSV is triggered from
       * handler mode (e.g., from SVC), EXC_RETURN doesn't reflect the thread's
       * stack pointer type. The thread's context is ALWAYS on PSP when using
       * PSP for user threads. The idle thread sets g_current_sp_ptr to NULL
       * to skip saving, which is handled by the cbz check above. */

      /* Get PSP - points to hardware frame pushed by exception entry */
      "mrs r0, psp                \n"

      /* Save thread's r4-r11 below hardware frame */
      "stmdb r0!, {r4-r11}        \n" /* Save r4-r11, r0 now points to software
                                         frame base */

      /* Store SW frame base directly to TCB */
      "str r0, [r1]               \n" /* current->sp = SW frame base */

      /* Memory barrier to ensure TCB write completes before continuing */
      "dsb                        \n"

      "1:                         \n" /* Load new thread context */

      "ldr r2, =g_next_sp_value   \n" /* r2 = &g_next_sp_value */

      /* DEBUG: Store g_next_sp_value address */
      "ldr r3, =pendsv_debug_r2_addr \n"
      "str r2, [r3]               \n"

      "ldr r0, [r2]               \n" /* r0 = g_next_sp_value (SW frame base) */

      /* DEBUG: Store loaded r0 value */
      "ldr r3, =pendsv_debug_r0_after_load \n"
      "str r0, [r3]               \n"

      /* CRITICAL: No function calls allowed in PendSV!
       * Calling printf() corrupts the stack and breaks register restore.
       * Simple NULL check only - no debug output in PendSV handler itself. */
      "cbz r0, 3f                 \n" /* Skip if NULL - no context switch */

      /* CRITICAL FIX: Restore r4-r11 from SW frame, r0 becomes HW frame pointer
       */
      "ldmia r0!, {r4-r11}        \n" /* Restore r4-r11, r0 now = SW_base + 32 =
                                         HW frame */

      /* Set PSP to hardware frame for exception return */
      "msr psp, r0                \n" /* PSP = SW_base + 32 = HW frame base */
      "dsb                        \n" /* Ensure PSP write completes */
      "isb                        \n"

      /* DEBUG: Read PSP back and store it */
      "mrs r3, psp                \n"
      "ldr r2, =pendsv_debug_psp_after_set \n"
      "str r3, [r2]               \n"

      /* Clear reschedule flag to mark completion.
       * CRITICAL: pok_context_switch() must ALWAYS update ALL globals (old_sp,
       * new_sp, flag), not conditionally based on this flag's state. */
      "ldr r1, =g_reschedule_needed \n"
      "movs r3, #0                \n"
      "strb r3, [r1]              \n"

      /* Ensure memory ops complete before return */
      "dsb                        \n"
      "isb                        \n"

      /* CRITICAL: Enable SysTick timer after first context switch completes.
       * Timer was configured but not enabled during pok_timer_init() to avoid
       * timer interrupts firing before PSP is set up. Now that PSP is loaded
       * with the first thread's stack, it's safe to start the timer.
       * We use a static flag to ensure we only do this once. */
      "ldr r1, =pendsv_timer_enabled \n"
      "ldrb r2, [r1]              \n"
      "cbnz r2, 2f                \n" /* Skip if already enabled */

      /* Enable SysTick: enable + interrupt + processor clock */
      "ldr r1, =0xE000E010        \n" /* SysTick CSR address */
      "movs r2, #7                \n" /* ENABLE | TICKINT | CLKSOURCE */
      "str r2, [r1]               \n"
      "dsb                        \n"
      "isb                        \n"

      /* Mark timer as enabled */
      "ldr r1, =pendsv_timer_enabled \n"
      "movs r2, #1                \n"
      "strb r2, [r1]              \n"
      "dsb                        \n"

      "2:                         \n"

      /* DEBUG: Store PSP value before decision */

      /* Check if new SP is in partition memory (RAM 0x20010000+) */
      /* Kernel threads use high RAM (0x2001F000+), partition threads use low
         RAM */
      "ldr r1, =kernel_stack_threshold \n" /* Load address of threshold constant
                                            */
      "ldr r1, [r1]               \n"      /* Load threshold value */
      "cmp r0, r1                 \n"      /* Compare SP to threshold */
      "bhs 4f                     \n" /* If SP >= threshold, kernel thread */

      /* DEBUG: Mark we're taking partition thread path */

      /* Partition thread: Force EXC_RETURN to Thread mode using PSP
         (0xFFFFFFFD) */
      "ldr lr, =0xFFFFFFFD        \n" /* EXC_RETURN: Thread mode, use PSP */

      /* DEBUG: Store EXC_RETURN value */

      "bx lr                      \n" /* Return from exception */

      "4:                         \n"
      /* Kernel/idle thread: Keep Handler mode with MSP */
      "bx lr                      \n" /* Return with original LR (Handler mode)
                                       */

      "3:                         \n"
      /* No context switch needed - return with original LR */
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

  /* NOTE: SVC priority left at default (high priority).
   * Context switches during syscalls are handled directly in SVC_Handler,
   * not via PendSV, to avoid tail-chaining complexities. */

  return POK_ERRNO_OK;
}
