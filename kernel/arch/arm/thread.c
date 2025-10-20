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

/* LIGHTWEIGHT REGISTER SNAPSHOTS - No function calls, just memory writes
 * Use these to capture register state at critical points without disturbing
 * execution */
struct register_snapshot {
  uint32_t r0, r1, r2, r3, r4, r5, r6, r7;
  uint32_t r8, r9, r10, r11, r12, sp, lr, pc;
  uint32_t psp, msp, primask, control;
  uint32_t g_reschedule_needed_val;
  uint32_t g_current_sp_ptr_val;
  uint32_t g_next_sp_value_val;
  uint32_t timestamp; /* Can be incremented to track ordering */
};

volatile struct register_snapshot
    __attribute__((used)) g_snapshot_before_ctx = {0};
volatile struct register_snapshot
    __attribute__((used)) g_snapshot_in_pendsv = {0};
volatile struct register_snapshot
    __attribute__((used)) g_snapshot_at_fault = {0};

/* Force linker to keep snapshot globals by creating a reference to them */
volatile struct register_snapshot *__snapshot_refs[3] __attribute__((used)) = {
    &g_snapshot_before_ctx, &g_snapshot_in_pendsv, &g_snapshot_at_fault};

/* Macro to capture complete register snapshot without function calls
 * Usage: SNAPSHOT_REGISTERS(g_snapshot_before_ctx, 1)
 * Parameters:
 *   dest - destination variable (must be struct register_snapshot)
 *   timestamp - value to store in timestamp field
 */
#define SNAPSHOT_REGISTERS(dest, timestamp_val)                                \
  __asm volatile(                                                              \
      "ldr r3, =" #dest "         \n"                                          \
      "str r0, [r3, #0]           \n" /* r0 */                                 \
      "str r1, [r3, #4]           \n" /* r1 */                                 \
      "str r2, [r3, #8]           \n" /* r2 */                                 \
      "str r3, [r3, #12]          \n" /* r3 (address of snapshot) */           \
      "str r4, [r3, #16]          \n" /* r4 */                                 \
      "str r5, [r3, #20]          \n" /* r5 */                                 \
      "str r6, [r3, #24]          \n" /* r6 */                                 \
      "str r7, [r3, #28]          \n" /* r7 */                                 \
      "str r8, [r3, #32]          \n" /* r8 */                                 \
      "str r9, [r3, #36]          \n" /* r9 */                                 \
      "str r10, [r3, #40]         \n" /* r10 */                                \
      "str r11, [r3, #44]         \n" /* r11 */                                \
      "str r12, [r3, #48]         \n" /* r12 */                                \
      "mov r0, sp                 \n"                                          \
      "str r0, [r3, #52]          \n" /* sp */                                 \
      "mov r0, lr                 \n"                                          \
      "str r0, [r3, #56]          \n" /* lr */                                 \
      "mov r0, pc                 \n"                                          \
      "str r0, [r3, #60]          \n" /* pc */                                 \
      "mrs r0, psp                \n"                                          \
      "str r0, [r3, #64]          \n" /* psp */                                \
      "mrs r0, msp                \n"                                          \
      "str r0, [r3, #68]          \n" /* msp */                                \
      "mrs r0, primask            \n"                                          \
      "str r0, [r3, #72]          \n" /* primask */                            \
      "mrs r0, control            \n"                                          \
      "str r0, [r3, #76]          \n" /* control */ /* Now capture global      \
                                                       variables */            \
      "ldr r0, =g_reschedule_needed \n"                                        \
      "ldrb r0, [r0]              \n"                                          \
      "str r0, [r3, #80]          \n" /* g_reschedule_needed */                \
      "ldr r0, =g_current_sp_ptr  \n"                                          \
      "ldr r0, [r0]               \n"                                          \
      "str r0, [r3, #84]          \n" /* g_current_sp_ptr */                   \
      "ldr r0, =g_next_sp_value   \n"                                          \
      "ldr r0, [r0]               \n"                                          \
      "str r0, [r3, #88]          \n" /* g_next_sp_value */                    \
      "movs r0, #" #timestamp_val "\n"                                         \
      "str r0, [r3, #92]          \n" /* timestamp */                          \
      ::                                                                       \
          : "r0", "r1", "r2", "r3", "memory")

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
#if 0 /* SPAM: CTX_SWITCH_ENTRY debug (49% of output) */
#ifdef POK_NEEDS_DEBUG
  /* DEBUG: Print parameters at function entry */
  pok_cons_write(">>>CTX_SWITCH_ENTRY: old_sp_param=0x", 37);
  char entry_hex[9];
  uint32_t entry_val = (uint32_t)(uintptr_t)old_sp;
  for (int i = 7; i >= 0; i--) {
    entry_hex[i] = "0123456789ABCDEF"[entry_val & 0xF];
    entry_val >>= 4;
  }
  entry_hex[8] = '\n';
  pok_cons_write(entry_hex, 9);
#endif
#endif /* End CTX_SWITCH_ENTRY spam block */

  /* CRITICAL: Enable timer on first context switch
   * Timer is deferred until now to avoid interrupt storm during partition
   * initialization. Once first thread is ready to execute, it's safe to enable
   * SysTick interrupts.
   *
   * IMPORTANT: Must disable interrupts during timer init to prevent the timer
   * interrupt from firing immediately and causing a nested/reentrant context
   * switch that would corrupt the global variables before PendSV can read them.
   */
  static uint8_t timer_initialized = 0;
  if (!timer_initialized) {
    timer_initialized = 1;
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("[CTX_SWITCH] Initializing timer on first context switch\n",
                   58);
#endif
    /* Disable interrupts to prevent timer firing during initialization */
    uint32_t primask_save;
    __asm volatile("mrs %0, PRIMASK" : "=r"(primask_save)::"memory");
    __asm volatile("cpsid i" ::: "memory");

    /* Forward declare pok_time_init */
    extern void pok_time_init(void);
    pok_time_init();

    /* Restore interrupt state - interrupts will be enabled later in this
     * function */
    if ((primask_save & 0x1u) == 0) {
      __asm volatile("cpsie i" ::: "memory");
    }
  }

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

  /* CRITICAL FIX: Always update ALL globals, not just when flag is clear.
   * Previous bug: Code used `if (!g_reschedule_needed)` to conditionally set
   * g_current_sp_ptr, but PendSV clears this flag after each switch. This
   * caused EVERY switch request to look like the "first" request, resulting in
   * g_current_sp_ptr pointing to the wrong thread (the thread we're switching
   * TO instead of the thread we're switching FROM).
   *
   * Correct behavior: ALWAYS set ALL three globals on EVERY switch request:
   * - g_current_sp_ptr = where to save current thread's context
   * - g_next_sp_value = which context to load for new thread
   * - g_reschedule_needed = 1 to trigger PendSV
   */
  /* Debug: Print what we're setting up (UNCONDITIONAL) */
  /* pok_cons_write("CTX_SETUP\n", 10); */

  g_current_sp_ptr = old_sp; /* ALWAYS update: thread to save */
  g_next_sp_value = new_sp;  /* ALWAYS update: thread to restore */

  /* CRITICAL: Memory barriers to ensure writes are visible before triggering
   * PendSV */
  __asm volatile(
      "dsb" ::
          : "memory"); /* Data Synchronization Barrier - wait for writes */
  __asm volatile("isb" ::
                     : "memory"); /* Instruction Synchronization Barrier - flush
                                     pipeline */

  /* Check if context switch is already in progress */
  uint8_t was_already_pending = g_reschedule_needed;
  g_reschedule_needed = 1; /* Set flag to trigger PendSV */

#if 0 /* SPAM: old_sp_ptr/new_sp debug (93.6% of remaining output) */
#ifdef POK_NEEDS_DEBUG
  /* Debug: Print detailed values */
  pok_cons_write("old_sp_ptr=0x", 13);
  char hex_buf[9];
  uint32_t val = (uint32_t)(uintptr_t)old_sp;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
    val >>= 4;
  }
  hex_buf[8] = ' ';
  pok_cons_write(hex_buf, 9);

  pok_cons_write("new_sp=0x", 9);
  val = new_sp;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
    val >>= 4;
  }
  hex_buf[8] = '\n';
  pok_cons_write(hex_buf, 9);
#endif
#endif /* End old_sp_ptr/new_sp spam block */

#if 0  /* SPAM: CTX_SWITCH debug (19.2% of output) */
  pok_cons_write("CTX_SWITCH: new_sp=0x", 21);
  val = new_sp;
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
#endif /* End CTX_SWITCH spam block */

#if 0  /* SPAM: CTX_VERIFY debug (14.1% of output) */
  /* DEBUG: Verify PC field in context before switching */
  if (new_sp != 0) {
    volatile uint32_t *ctx_ptr = (volatile uint32_t *)new_sp;
    pok_cons_write("CTX_VERIFY: PC at [sp+56]=0x", 28);
    val = ctx_ptr[14]; /* PC is at offset 56 = word 14 */
    for (int i = 7; i >= 0; i--) {
      hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
      val >>= 4;
    }
    hex_buf[8] = ' ';
    pok_cons_write(hex_buf, 9);

    pok_cons_write("LR at [sp+52]=0x", 16);
    val = ctx_ptr[13]; /* LR is at offset 52 = word 13 */
    for (int i = 7; i >= 0; i--) {
      hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
      val >>= 4;
    }
    hex_buf[8] = '\n';
    pok_cons_write(hex_buf, 9);
  }
#endif /* End CTX_VERIFY spam block */

  /* Ensure memory operations complete before triggering PendSV */
  __asm volatile("dsb" ::: "memory");

  /* SNAPSHOT: Capture register state BEFORE triggering PendSV (no function
   * calls) */
  SNAPSHOT_REGISTERS(g_snapshot_before_ctx, 1);

#if 0 /* SPAM: TRIGGER_PSV debug (14.0% of output) */
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
#endif /* End TRIGGER_PSV spam block */

  /* Trigger PendSV exception to perform context switch.
   * CRITICAL: Only trigger if not already pending, to avoid clearing the
   * PendSV bit while the handler is still running (which would lose the
   * request).
   */
  if (!was_already_pending) {
    *SCB_ICSR |= SCB_ICSR_PENDSVSET;

    /* Memory barrier to ensure PendSV is triggered */
    __asm volatile("dsb; isb" ::: "memory");
  }

#if 0 /* SPAM: ICSR_after debug (14.0% of output) */
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
#endif /* End ICSR_after spam block */

  /* Enable interrupts to allow PendSV to run.
   * PendSV will tail-chain directly from SVC exit if we're in a syscall
   * handler. No "thread-mode gap" exists - the hardware handles this
   * atomically.
   *
   * CRITICAL: After enabling interrupts, if PendSV is pending, it will run
   * IMMEDIATELY. If the context switch succeeds, PendSV will NOT return here
   * - it will return to the NEW thread instead. So code after this point
   * should ONLY execute if PendSV took an early exit (no switch needed).
   */
  if ((primask & 0x1u) == 0) {
    __asm volatile("cpsie i" ::: "memory");
  }

#if 0 /* SPAM: POST_ENABLE_INT debug (14.1% of output) */
#ifdef POK_NEEDS_DEBUG
  /* DEBUG: If we reach here, PendSV either hasn't run yet or took an early exit.
   * Check if we're still in the old thread or if switch happened. */
  pok_cons_write("POST_ENABLE_INT: still here\n", 29);
#endif
#endif /* End POST_ENABLE_INT spam block */
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
