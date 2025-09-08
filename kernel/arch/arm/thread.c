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
 * \file    thread.c
 * \brief   ARM Cortex-M thread management and context switching
 * \author  POK team
 */

/* POK system headers */
#include <errno.h>
#include <libc.h>
#include <stddef.h>
#include <stdint.h>

/* POK core headers */
#include <bsp.h>
#include <core/sched.h>
#include <core/thread.h>

/* Architecture-specific headers */
#include "arch.h"
#include "cortex_m_config.h"
#include "nvic.h"
#include "thread.h"

#define STACK_ALIGNMENT CORTEX_M_STACK_ALIGNMENT
#define STACK_ALIGNMENT_MASK CORTEX_M_STACK_ALIGNMENT_MASK

/**
 * Create a thread context with proper ARM Cortex-M stack frame
 *
 * @param thread_id Unique identifier for the thread
 * @param stack_size Size of stack to allocate in bytes
 * @param entry Entry point function address for the thread
 * @return Context pointer on success, 0 on failure
 */
uint32_t pok_context_create(uint32_t thread_id, uint32_t stack_size,
                            uintptr_t entry) {
  start_context_t *sp;
  char *stack_addr;

  /* Validate minimum stack size BEFORE allocating memory to prevent leak */
  if (stack_size < STACK_ALIGNMENT + sizeof(start_context_t)) {
#ifdef POK_NEEDS_DEBUG
    printf("Error: stack_size %u too small, minimum required: %u\n", stack_size,
           (unsigned)(STACK_ALIGNMENT + sizeof(start_context_t)));
#endif
    return 0; /* Fail context creation for insufficient stack */
  }

  stack_addr = pok_bsp_mem_alloc(stack_size);
  if (!stack_addr) {
    return (0);
  }

  /* Place context at top of stack - enforce Cortex-M alignment downward */
  uint32_t stack_top =
      ((uint32_t)(uintptr_t)stack_addr + stack_size - sizeof(start_context_t)) &
      ~STACK_ALIGNMENT_MASK;
  sp = (start_context_t *)stack_top;

  memset(sp, 0, sizeof(start_context_t));

  /* Initialize context for thread startup */
  sp->ctx.pc = (uint32_t)pok_arch_thread_start; /* Start with thread wrapper */
  sp->ctx.lr =
      (uint32_t)pok_arch_thread_exit_stub; /* Exit stub for thread return */
  sp->ctx.xpsr = 0x01000000;               /* Thumb bit set */
  sp->ctx.r0 = (uint32_t)sp;               /* Pass context pointer via R0 */

  /* CRITICAL FIX: PSP must point to the hardware frame for exception return
   * The thread's SP field should contain the PSP value that points to where
   * hardware exception return will find the context frame.
   *
   * Stack layout (high to low address):
   * [stack_addr + stack_size] <- stack top
   * [...user stack space...]
   * [hardware frame: xpsr,pc,lr,r12,r3,r2,r1,r0] <- 8 words (32 bytes)
   * [software frame: r11,r10,r9,r8,r7,r6,r5,r4] <- 8 words (32 bytes)
   * [start_context_t] <- our context structure
   *
   * PSP should point to hardware frame (r0) for proper exception return
   */
  /* Initial PSP points to start of hardware frame (r0) */
  uint32_t initial_psp = (uint32_t)(uintptr_t)&sp->ctx.r0;

  /* Bounds: ensure software frame [r4-r11] and hardware frame [r0..xpsr] fit */
  uint32_t stack_base = (uint32_t)(uintptr_t)stack_addr;

  /* SECURITY: Check for 32-bit overflow in stack_limit calculation */
  uint32_t stack_limit;
  if (stack_size > UINT32_MAX - stack_base) {
    /* Overflow would occur - invalid stack configuration */
#ifdef POK_NEEDS_DEBUG
    printf("Error: stack configuration would cause 32-bit overflow\n");
#endif
    pok_bsp_mem_free(stack_addr, stack_size);
    return 0;
  }
  stack_limit = stack_base + stack_size;

  uint32_t hw_frame_end = initial_psp + (8U * sizeof(uint32_t)); /* past xpsr */
  uint32_t sw_frame_start = (uint32_t)(uintptr_t)&sp->ctx.r4; /* lowest addr */

  /* Additional hardening: check hw_frame_end calculation for overflow */
  if (initial_psp > UINT32_MAX - (8U * sizeof(uint32_t))) {
#ifdef POK_NEEDS_DEBUG
    printf("Error: hardware frame end calculation would overflow\n");
#endif
    pok_bsp_mem_free(stack_addr, stack_size);
    return 0;
  }

  if (sw_frame_start < stack_base || hw_frame_end > stack_limit) {
#ifdef POK_NEEDS_DEBUG
    printf("Error: context frames out of stack bounds [0x%08x, 0x%08x)\n",
           stack_base, stack_limit);
#endif
    pok_bsp_mem_free(stack_addr, stack_size);
    return 0;
  }

  /* PSP management is handled externally - context structure only contains
   * register state */

  sp->entry = entry;
  sp->id = thread_id;

  /* Return the initial PSP value pointing to the hardware frame
   * This value will be stored in pok_threads[].sp and used by context switcher
   */
  return initial_psp;
}

/* Global variables for PendSV context switching - accessed by PendSV handler */
uint32_t *volatile g_old_sp_ptr = NULL;
volatile uint32_t g_new_sp = 0;
/**
 * Perform ARM Cortex-M context switch between threads
 *
 * Uses PendSV exception for proper atomic context switching.
 * This function sets up the context switch parameters and triggers PendSV.
 * The actual context switch happens in the PendSV handler.
 *
 * @param old_sp Pointer to store current thread's stack pointer
 * @param new_sp Stack pointer of thread to switch to
 */
void pok_context_switch(uint32_t *old_sp, uint32_t new_sp) {
  if (old_sp == NULL) {
    /* Clear global variables to prevent stale values from affecting future
     * context switches */
    g_old_sp_ptr = NULL;
    g_new_sp = 0;
    return;
  }

  /* SECURITY: Verify new_sp is a valid stack pointer value
   * Basic sanity checks to prevent malicious or corrupted stack pointers */
  if (new_sp == 0 || (new_sp & 0x3) != 0) {
    /* Invalid: NULL or non-word-aligned stack pointer */
#ifdef POK_NEEDS_DEBUG
    printf("Error: invalid stack pointer in context switch: 0x%08x\n", new_sp);
#endif
    return;
  }

  /* Additional check: ensure new_sp is in reasonable memory range
   * ARM Cortex-M typically uses 0x20000000+ for SRAM */
  if (new_sp < 0x20000000 || new_sp >= 0x30000000) {
#ifdef POK_NEEDS_DEBUG
    printf("Warning: suspicious stack pointer outside SRAM range: 0x%08x\n",
           new_sp);
#endif
    /* Continue but log the warning - might be valid in some configurations */
  }

  /* Disable interrupts to prevent race conditions during handoff */
  uint32_t primask;
  __asm volatile("mrs %0, PRIMASK" : "=r"(primask)::"memory");
  __asm volatile("cpsid i" ::: "memory");

  /* Set up context switch parameters for PendSV handler */
  g_old_sp_ptr = old_sp;
  g_new_sp = new_sp;

  /* Ensure memory operations complete before triggering PendSV */
  __asm volatile("dsb" ::: "memory");

  /* Trigger PendSV exception to perform context switch
   * Use bit-specific write to avoid clearing other SCB_ICSR bits
   */
  SCB_ICSR |= SCB_ICSR_PENDSVSET; /* write-1 to set PendSV pending */

  /* Memory barrier to ensure PendSV is triggered */
  __asm volatile("dsb; isb" ::: "memory");

  /* Restore previous interrupt state */
  if ((primask & 0x1u) == 0) {
    __asm volatile("cpsie i" ::: "memory");
  }
}

void pok_context_reset(uint32_t stack_size, uint32_t stack_addr) {
  start_context_t *sp;
  uint32_t id;
  uint32_t entry;

  /* Validate minimum stack size to prevent underflow - same as
   * pok_context_create */
  if (stack_size < STACK_ALIGNMENT + sizeof(start_context_t)) {
#ifdef POK_NEEDS_DEBUG
    printf("Error: reset stack_size %u too small, minimum required: %u\n",
           stack_size, (unsigned)(STACK_ALIGNMENT + sizeof(start_context_t)));
#endif
    return; /* Cannot safely reset context */
  }

  sp = (start_context_t *)(((uintptr_t)stack_addr + stack_size -
                            sizeof(start_context_t)) &
                           ~STACK_ALIGNMENT_MASK);

  /* Preserve thread information */
  id = sp->id;
  entry = sp->entry;

  /* Reset context */
  memset(sp, 0, sizeof(start_context_t));

  sp->ctx.pc = (uint32_t)pok_arch_thread_start;
  sp->ctx.lr =
      (uint32_t)pok_arch_thread_exit_stub; /* Exit stub for thread return */
  sp->ctx.xpsr = 0x01000000;
  sp->ctx.r0 = (uint32_t)sp; /* Pass context pointer via R0 */

  /* Verify that hardware frame fits within stack bounds
   * (PSP management is handled externally) */
  uint32_t initial_psp =
      (uint32_t)&sp->ctx.r0; /* Points to start of hardware frame */
  uint32_t frame_end = initial_psp + (8 * sizeof(uint32_t));
  if (frame_end > (uint32_t)((uintptr_t)stack_addr + stack_size)) {
#ifdef POK_NEEDS_DEBUG
    printf("Error: reset hardware frame extends beyond stack bounds [0x%08x, "
           "0x%08x)\n",
           (uint32_t)(uintptr_t)stack_addr,
           (uint32_t)((uintptr_t)stack_addr + stack_size));
#endif
    return; /* Cannot safely reset context */
  }

  /* PSP management is handled externally - context structure only contains
   * register state */

  sp->entry = entry;
  sp->id = id;
}

/*
 * Thread exit stub - naked assembly wrapper for thread termination
 * This function is used as the LR value for threads to handle proper
 * termination if the thread function returns.
 */
void __attribute__((naked, used)) pok_arch_thread_exit_stub(void) {
  __asm volatile(
      /* Call the thread termination handler */
      "bl pok_arch_thread_exit_handler    \n"
      /* Should never return, but loop if it does */
      "1:                                 \n"
      "  wfi                              \n"
      "  b 1b                             \n"
      :
      :
      : "memory");
}

/*
 * Thread exit handler - called when thread function returns
 */
static void pok_arch_thread_exit_handler(void) {
#ifdef POK_NEEDS_DEBUG
  printf("FATAL: Thread returned from entry function - terminating\n");
#endif

  /* Terminate this thread safely through the POK scheduler */
  pok_sched_stop_self(); /* Terminate this thread properly */

  /* If pok_sched_stop_self returns (which should not happen),
   * enter safe infinite loop without disabling global interrupts */
  while (1) {
    __asm volatile("wfi"); /* Wait for interrupt (low power) */
  }
}

/*
 * Thread startup wrapper
 * This function is called when a new thread starts execution
 */
void pok_arch_thread_start(start_context_t *ctx) {
  uint32_t entry, thread_id;

  /* Extract thread information */
  entry = ctx->entry;
  thread_id = ctx->id;

  /* Call POK core thread start function
   * NOTE: If this function returns, the thread's LR register will cause
   * a branch to pok_arch_thread_exit_stub() which handles termination properly.
   */
  pok_thread_start((void (*)(void))entry, thread_id);

  /* NOTE: Execution should not reach here as pok_thread_start() should not
   * return. If it does return, the ARM exception return mechanism will use the
   * LR register (set to pok_arch_thread_exit_stub) to handle thread termination
   * safely.
   */
}
