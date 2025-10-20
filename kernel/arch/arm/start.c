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
 * \file kernel/arch/arm/start.c
 * \brief ARM Cortex-M first thread startup
 * \author POK team
 */

#include <core/debug.h>
#include <core/sched.h>
#include <core/thread.h>
#include <core/time.h>
#include <types.h>

/* External thread structures and scheduler state */
extern pok_thread_t pok_threads[];
extern uint32_t current_threads[];

/**
 * Start first thread - ARM Cortex-M specific initialization
 *
 * This function performs the critical sequence needed to start multitasking:
 * 1. Run scheduler to select the first thread
 * 2. Load PSP with the first thread's hardware stack frame
 * 3. Initialize the timer (enables SysTick interrupts)
 * 4. Drop to thread mode using PSP
 *
 * CRITICAL: This must be called BEFORE any interrupts are enabled, otherwise
 * we'll be stuck in handler mode forever and threads will never execute.
 *
 * This function does NOT return - it transitions to the first thread's entry
 * point.
 */
void __attribute__((noreturn)) pok_arch_start_first_thread(void) {
#ifdef POK_NEEDS_DEBUG
  printf("%s", "[START] Selecting first thread\n");
#endif

  /* Run scheduler to select first thread - this sets current_threads[0] */
  pok_sched();

  /* Get the elected thread's SP from pok_threads array */
  uint32_t current_tid = POK_SCHED_CURRENT_THREAD;
  uint32_t elected_sp = pok_threads[current_tid].sp;

  if (elected_sp == 0) {
    /* Scheduler returned no valid thread - this is a fatal error */
#ifdef POK_NEEDS_DEBUG
    printf("%s", "[START] FATAL: No thread selected by scheduler\n");
#endif
    while (1) {
      __asm volatile("wfi"); /* Halt */
    }
  }

#ifdef POK_NEEDS_DEBUG
  printf("[START] First thread selected, tid=%u SP=0x%x\n", current_tid,
         elected_sp);
#endif

  /* elected_sp points to the software frame (r4-r11)
   * Hardware frame (r0-r3, r12, LR, PC, xPSR) is at elected_sp + 32 */
#ifdef POK_NEEDS_DEBUG
  uint32_t hw_frame = elected_sp + 32;
  printf("[START] Loading PSP=0x%x\n", hw_frame);
#endif

  /* DO NOT initialize timer here - it will fire immediately!
   * Timer init is deferred until after first context switch completes.
   * See pok_sched_context_switch() which enables timer after first switch. */

#ifdef POK_NEEDS_DEBUG
  printf("%s", "[START] Dropping to thread mode (timer deferred)\n");
#endif

  /* CRITICAL: pok_sched() has already triggered a context switch via PendSV.
   * PendSV loaded the first thread's context and set PSP.
   * DO NOT perform another context load here - it would overwrite PSP!
   *
   * Instead, we need to perform an exception return to thread mode.
   * The first thread's context is already loaded by PendSV.
   * We just need to drop from handler mode to thread mode.
   */
  __asm volatile(
      /* Disable interrupts during the transition */
      "cpsid i                    \n"

      /* Set LR to EXC_RETURN for thread mode using PSP (0xFFFFFFFD) */
      "ldr lr, =0xFFFFFFFD        \n"

      /* Re-enable interrupts - timer is now started so SysTick can fire */
      "cpsie i                    \n"

      /* Exception return - hardware loads PC from [PSP+24] and enters thread
       * mode PSP was already set by PendSV, so this will jump to the first
       * thread. */
      "bx lr                      \n"

      : /* no outputs */
      : /* no inputs */
      : "memory", "lr");

  /* Should never reach here */
  while (1) {
    __asm volatile("wfi");
  }
}
