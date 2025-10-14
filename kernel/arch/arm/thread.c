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

/* POK core headers */
#include <bsp.h>
#include <core/sched.h>
#include <core/thread.h>

/* Architecture-specific headers */
#include "arch.h"
#include "cortex_m_config.h"
#include "memory_config.h"
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
    /* Error: stack_size too small - printf removed due to linker issues */
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

  /* Initialize context for thread startup
   * For ARM with MPU, threads start directly at their entry point
   * since they cannot execute kernel code (pok_arch_thread_start) */
  sp->ctx.pc = entry; /* Start at partition entry point */
  sp->ctx.lr =
      (uint32_t)pok_arch_thread_exit_stub; /* Exit stub for thread return */
  sp->ctx.xpsr = 0x01000000;               /* Thumb bit set */
  sp->ctx.r0 = 0; /* Clear R0 (no context pointer needed) */

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

#ifdef POK_NEEDS_DEBUG
  /* Debug: Verify frame initialization */
  uint32_t *sw_frame = (uint32_t *)((uint32_t)initial_psp - 32);
  uint32_t *hw_frame = (uint32_t *)initial_psp;
  printf("FRAME_INIT: sp_struct=0x%x initial_psp=0x%x\n", (uint32_t)sp,
         initial_psp);
  printf("FRAME_INIT: sw_frame=0x%x [r4-r11]: %x %x %x %x %x %x %x %x\n",
         (uint32_t)sw_frame, sw_frame[0], sw_frame[1], sw_frame[2], sw_frame[3],
         sw_frame[4], sw_frame[5], sw_frame[6], sw_frame[7]);
  printf("FRAME_INIT: hw_frame=0x%x [r0-xpsr]: %x %x %x %x %x %x %x %x\n",
         (uint32_t)hw_frame, hw_frame[0], hw_frame[1], hw_frame[2], hw_frame[3],
         hw_frame[4], hw_frame[5], hw_frame[6], hw_frame[7]);
#endif

  /* Bounds: ensure software frame [r4-r11] and hardware frame [r0..xpsr] fit */
  uint32_t stack_base = (uint32_t)(uintptr_t)stack_addr;

  /* SECURITY: Check for 32-bit overflow in stack_limit calculation */
  uint32_t stack_limit;
  if (stack_size > UINT32_MAX - stack_base) {
    /* Overflow would occur - invalid stack configuration */
#ifdef POK_NEEDS_DEBUG
    /* Error: stack configuration would cause 32-bit overflow */
#endif
    pok_bsp_mem_free(stack_addr, stack_size);
    return 0;
  }
  stack_limit = stack_base + stack_size;

  /* Validate that both software (r4-r11) and hardware (r0..xpsr) frames fit
   * within the allocated stack */
  if (initial_psp > UINT32_MAX - (8U * sizeof(uint32_t))) {
#ifdef POK_NEEDS_DEBUG
    /* Error: hardware frame end calculation would overflow */
#endif
    pok_bsp_mem_free(stack_addr, stack_size);
    return 0;
  }
  uint32_t hw_frame_start = initial_psp;
  uint32_t hw_frame_end = initial_psp + (8U * sizeof(uint32_t)); /* past xpsr */
  uint32_t sw_frame_start =
      hw_frame_start -
      (8U * sizeof(uint32_t)); /* r4-r11 below hardware frame */
  if (sw_frame_start < stack_base || hw_frame_end > stack_limit) {
#ifdef POK_NEEDS_DEBUG
    /* Error: context frames out of stack bounds */
#endif
    pok_bsp_mem_free(stack_addr, stack_size);
    return 0;
  }

  /* PSP management is handled externally - context structure only contains
   * register state */

  sp->entry = entry;
  sp->id = thread_id;

  /* CRITICAL FIX: Return SW frame base pointer, not HW frame pointer
   * TCB must store pointer to where r4-r11 are saved (SW frame base).
   * PendSV will restore r4-r11 from this address, then set PSP to (this + 32)
   * for hardware to restore r0-xpsr on exception return.
   */
  uint32_t sw_frame_base = initial_psp - 32;
  return sw_frame_base;
}

/* Global variables for PendSV context switching - accessed by PendSV handler
 *
 * IDEMPOTENT RESCHEDULE PATTERN:
 * Instead of ephemeral parameters that get overwritten before PendSV runs,
 * we use an idempotent scheduling flag with stable storage.
 * PendSV reads these values when it executes (via tail-chaining after SVC).
 * Multiple reschedule requests update the values - "last write wins".
 * This follows the standard M-profile RTOS pattern.
 */
volatile uint8_t g_reschedule_needed = 0;
volatile uint32_t *g_current_sp_ptr =
    NULL; /* Pointer to current thread's SP field in TCB */
volatile uint32_t g_next_sp_value =
    0; /* SP value to load for next thread (stable storage) */

/* Debug variables to track PendSV operations */
volatile uint32_t g_debug_loaded_sp =
    0; /* SP value loaded from g_next_sp_value */
volatile uint32_t g_debug_final_psp = 0; /* Final PSP value after ldmia */
volatile uint32_t g_debug_saved_from_msp =
    0; /* PSP value when saving from MSP mode */
volatile uint32_t g_debug_sw_frame_addr =
    0; /* Software frame address before restore */
volatile uint32_t g_debug_pendsv_psp =
    0; /* PSP value before EXC_RETURN decision */
volatile uint8_t g_debug_pendsv_path = 0; /* 0=kernel, 1=partition path */
volatile uint32_t g_debug_pendsv_lr = 0;  /* EXC_RETURN value loaded into LR */
volatile uint8_t g_debug_restore_marker =
    0; /* Marker to show restore path was entered */
volatile uint32_t g_debug_next_sp_addr = 0; /* Address of g_next_sp_value */
volatile uint32_t g_debug_next_sp_val =
    0; /* Value loaded from g_next_sp_value */
volatile uint8_t g_debug_pendsv_entry =
    0; /* Marker to show PendSV was entered */
volatile uint32_t g_debug_resched_addr = 0; /* Address of g_reschedule_needed */
volatile uint8_t g_debug_resched_val =
    0; /* Value of g_reschedule_needed in PendSV */
volatile uint32_t g_debug_ctx_pc = 0; /* PC value from context before restore */
volatile uint32_t g_debug_ctx_xpsr =
    0; /* xPSR value from context before restore */
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
  /* SECURITY: Verify new_sp FIRST - it's required for any context switch
   * Basic sanity checks to prevent malicious or corrupted stack pointers */
  if (new_sp == 0 || (new_sp & 0x3) != 0) {
    /* Invalid: NULL or non-word-aligned stack pointer - abort and clear state
     */
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("CTX_SWITCH_ERR: invalid new_sp\n", 32);
#endif
    /* Clear global variables within interrupt-disabled region */
    uint32_t primask;
    __asm volatile("mrs %0, PRIMASK" : "=r"(primask)::"memory");
    __asm volatile("cpsid i" ::: "memory");

    g_reschedule_needed = 0;
    g_current_sp_ptr = NULL;
    g_next_sp_value = 0;

    /* Restore previous interrupt state */
    if ((primask & 0x1u) == 0) {
      __asm volatile("cpsie i" ::: "memory");
    }
    return;
  }

  /* NOTE: old_sp CAN be NULL when switching from idle/kernel thread (uses MSP).
   * This is VALID - PendSV will skip saving and just load the new thread. */

  /* Additional check: ensure new_sp is in reasonable memory range */
  if (new_sp < POK_SRAM_BASE || new_sp >= (POK_SRAM_BASE + POK_SRAM_SIZE)) {
#ifdef POK_NEEDS_DEBUG
    /* Warning: suspicious stack pointer outside configured SRAM range */
#endif
    /* Continue but log the warning - might be valid in some configurations */
  }

  /* Disable interrupts to prevent race conditions during handoff */
  uint32_t primask;
  __asm volatile("mrs %0, PRIMASK" : "=r"(primask)::"memory");
  __asm volatile("cpsid i" ::: "memory");

  /* IDEMPOTENT RESCHEDULE PATTERN:
   * Set the reschedule flag and update scheduling state. The KEY insight:
   * - g_current_sp_ptr is set ONLY on the FIRST request (when flag is 0)
   * - g_next_sp_value is updated on EVERY request - "last write wins"
   * - This ensures PendSV always saves to the ORIGINAL thread's SP,
   *   not to an intermediate thread's SP from a later request
   *
   * When multiple switches are requested before PendSV runs:
   * 1. First call: Sets g_current_sp_ptr = thread_A.sp, g_next_sp_value =
   * thread_B.sp
   * 2. Second call: Keeps g_current_sp_ptr = thread_A.sp, updates
   * g_next_sp_value = thread_C.sp
   * 3. PendSV: Saves thread_A context, loads thread_C context (correct!)
   *
   * This follows the standard M-profile RTOS pattern and eliminates the
   * parameter corruption issue.
   */
  if (!g_reschedule_needed) {
    /* First reschedule request - set current thread's SP pointer */
    g_current_sp_ptr = old_sp;
  }
  /* Always update next thread's SP (last write wins) */
  g_next_sp_value = new_sp;
  g_reschedule_needed = 1;

#ifdef POK_NEEDS_DEBUG
  pok_cons_write("CTX_SWITCH: new_sp=0x", 21);
  char hex_buf[9];
  uint32_t val = new_sp;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
    val >>= 4;
  }
  hex_buf[8] = ' ';
  pok_cons_write(hex_buf, 9);

  pok_cons_write("cur_sp_ptr=0x", 13);
  val = (uint32_t)(uintptr_t)g_current_sp_ptr;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
    val >>= 4;
  }
  hex_buf[8] = '\n';
  pok_cons_write(hex_buf, 9);
#endif

  /* Ensure memory operations complete before triggering PendSV */
  __asm volatile("dsb" ::: "memory");

#ifdef POK_NEEDS_DEBUG
  /* DEBUG: Check SCB_ICSR before triggering PendSV */
  pok_cons_write("TRIGGER_PSV: ICSR_before=0x", 27);
  uint32_t icsr_before = *SCB_ICSR;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[(icsr_before >> (i * 4)) & 0xF];
  }
  hex_buf[8] = '\n';
  pok_cons_write(hex_buf, 9);
#endif

  /* Trigger PendSV exception to perform context switch */
  *SCB_ICSR |= SCB_ICSR_PENDSVSET;

  /* Memory barrier to ensure PendSV is triggered */
  __asm volatile("dsb; isb" ::: "memory");

#ifdef POK_NEEDS_DEBUG
  /* DEBUG: Check SCB_ICSR after triggering PendSV */
  pok_cons_write("  ICSR_after=0x", 14);
  uint32_t icsr_after = *SCB_ICSR;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[(icsr_after >> (i * 4)) & 0xF];
  }
  hex_buf[8] = '\n';
  pok_cons_write(hex_buf, 9);
#endif

  /* Enable interrupts to allow PendSV to run.
   * PendSV will tail-chain directly from SVC exit if we're in a syscall
   * handler. No "thread-mode gap" exists - the hardware handles this
   * atomically.
   */
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
    /* Error: reset stack_size too small */
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
    /* Error: reset hardware frame extends beyond stack bounds */
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
static void __attribute__((used)) pok_arch_thread_exit_handler(void) {
  /* Debug output for thread termination */
  pok_cons_write("ARM_THREAD_EXIT: Thread terminated\n", 35);

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

  /* Debug output for thread execution */
  pok_cons_write("ARM_THREAD_START: Thread ", 26);
  char hex_buf[16];
  uint32_t val = thread_id;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
    val >>= 4;
  }
  hex_buf[8] = ' ';
  pok_cons_write(hex_buf, 9);

  pok_cons_write("entry=0x", 8);
  val = entry;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
    val >>= 4;
  }
  hex_buf[8] = '\n';
  pok_cons_write(hex_buf, 9);

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
