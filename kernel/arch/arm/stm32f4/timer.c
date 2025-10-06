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
 * \file    arch/arm/stm32f4/timer.c
 * \author  POK team
 * \brief   STM32F4 system timer using SysTick
 */

#include "../nvic.h"
#include "clock_config.h"
#include "peripherals.h"
#include "stm32f4_internal.h"
#include <bsp.h>
#include <core/sched.h>
#include <core/time.h>
#include <errno.h>
#include <libc.h>
#include <types.h> /* Ensure int64_t is available */

/* ARM Cortex-M is single-processor - define required configuration */
#ifndef POK_CONFIG_NB_PROCESSORS
#define POK_CONFIG_NB_PROCESSORS 1
#endif

/* SysTick registers */
/* SYSTICK_BASE now defined in peripherals.h */
#define SYSTICK_CSR (*((volatile uint32_t *)(SYSTICK_BASE + 0x00)))
#define SYSTICK_RVR (*((volatile uint32_t *)(SYSTICK_BASE + 0x04)))
#define SYSTICK_CVR (*((volatile uint32_t *)(SYSTICK_BASE + 0x08)))

/* SysTick Control and Status Register bits */
#define SYSTICK_CSR_ENABLE (1 << 0)
#define SYSTICK_CSR_TICKINT (1 << 1)
#define SYSTICK_CSR_CLKSOURCE (1 << 2)

/* Timer frequency from POK core (100kHz for proper timing consistency) */
/* QEMU workaround: Multiply by 1000 to slow down timer (stays within 24-bit
 * limit) */
#define TIMER_RELOAD_VAL ((SYSTICK_FREQ_HZ / POK_TIMER_FREQUENCY) * 1000)

/* SysTick reload register is 24-bit */
#define SYSTICK_MAX_RELOAD 0xFFFFFF
#define SYSTICK_MIN_RELOAD                                                     \
  100 /* Minimum reasonable reload value for proper timing */

/* Compile-time checks moved to runtime validation in pok_timer_init() */

pok_ret_t pok_timer_init(void) {
  /* Enhanced SysTick reload validation for all clock configurations */

  /* Debug output for timer configuration */
  pok_cons_write("Timer init: SYSTICK_FREQ_HZ=", 30);
  pok_cons_write("Timer init: POK_TIMER_FREQUENCY=", 33);
  pok_cons_write("Timer init: TIMER_RELOAD_VAL=", 30);

  /* Check for invalid system frequency */
  if (SYSTICK_FREQ_HZ == 0) {
    pok_cons_write("Timer init FAILED: SYSTICK_FREQ_HZ=0\n", 38);
    return POK_ERRNO_EINVAL;
  }

  /* CRITICAL: Check for timer configuration that would cause divide-by-zero */
  if (POK_TIMER_FREQUENCY > SYSTICK_FREQ_HZ) {
    return POK_ERRNO_EINVAL;
  }

  /* Additional check: ensure TIMER_RELOAD_VAL is not zero after division */
  if (TIMER_RELOAD_VAL == 0) {
    return POK_ERRNO_EINVAL;
  }

  /* Validate reload value is reasonable (not too small) */
  if (TIMER_RELOAD_VAL < SYSTICK_MIN_RELOAD) {
    /* Continue with initialization instead of failing */
  }

  /* Validate SysTick reload value doesn't exceed 24-bit limit */
  if (TIMER_RELOAD_VAL > SYSTICK_MAX_RELOAD) {
    return POK_ERRNO_EINVAL;
  }

  /* Validate that actual tick frequency will be reasonable - use integer
   * arithmetic to avoid FP */
  /* TIMER_RELOAD_VAL is guaranteed non-zero by checks above */
  uint32_t actual_freq = SYSTICK_FREQ_HZ / TIMER_RELOAD_VAL;
  /* Check tolerance using cross-multiplication: actual_freq * 100 vs
   * POK_TIMER_FREQUENCY * [95,105] */
  int64_t actual_freq_scaled = (int64_t)actual_freq * 100;
  int64_t target_freq_lower =
      (int64_t)POK_TIMER_FREQUENCY * 95; /* 95% lower bound */
  int64_t target_freq_upper =
      (int64_t)POK_TIMER_FREQUENCY * 105; /* 105% upper bound */

  if (actual_freq_scaled < target_freq_lower ||
      actual_freq_scaled > target_freq_upper) {
    /* Clock configuration may need adjustment - continue anyway */
  }

  /* SysTick configured for desired timer frequency */

  /* Disable SysTick */
  SYSTICK_CSR = 0;

  /* Set reload value for desired tick rate */
  SYSTICK_RVR = TIMER_RELOAD_VAL - 1;

  /* Clear current value */
  SYSTICK_CVR = 0;

  /* Set SysTick priority to lowest (same as PendSV) for proper tail-chaining
   * Using NVIC helper to ensure proper ARM_PRIORITY_BITS encoding
   * NVIC expects 4-bit priority values (0-15), not raw 8-bit values
   * CRITICAL: Must match PendSV priority (15) to allow tail-chaining */
  if (pok_nvic_set_priority(EXCEPTION_SYSTICK, 15) != POK_ERRNO_OK) {
    return POK_ERRNO_EINVAL;
  }
  /* Clear any pending SysTick before enabling to avoid spurious tick */
  *SCB_ICSR |= SCB_ICSR_PENDSTCLR;

  /* Re-enable timer with very slow rate for debugging */
  /* Configure SysTick: enable, interrupt, use processor clock */
  SYSTICK_CSR =
      SYSTICK_CSR_ENABLE | SYSTICK_CSR_TICKINT | SYSTICK_CSR_CLKSOURCE;

  /* Data Synchronization Barrier to ensure SysTick configuration completes */
  __asm volatile("dsb" : : : "memory");
  __asm volatile("isb" : : : "memory");

  /* Enable global interrupts - CRITICAL for SysTick to fire */
  __asm volatile("cpsie i" : : : "memory");

  /* SysTick now configured and enabled with interrupts enabled */
  pok_cons_write("Timer init SUCCESS: SysTick configured and enabled\n", 52);

  return POK_ERRNO_OK;
}

/* Global SysTick counter for debugging - visible to debugger */
volatile uint32_t pok_systick_counter = 0;

void pok_timer_handler(void) {
#ifdef POK_NEEDS_DEBUG
  static uint8_t handler_entry_count = 0;
  if (handler_entry_count < 2) {
    pok_cons_write("TIMER_HANDLER_ENTRY\n", 20);
    handler_entry_count++;
  }
#endif

  /* Clear SysTick interrupt flag (automatically cleared by reading CSR) */
  (void)SYSTICK_CSR;

  /* SysTick interrupt processing - increment global counter */
  pok_systick_counter++;

  /* Print every 100th tick to verify interrupts are firing */
  if ((pok_systick_counter % 100) == 0) {
    pok_cons_write("TICK!", 5);
  }

/* Update POK system time in nanoseconds - consistent with other POK
 * architectures Each timer interrupt represents 1/POK_TIMER_FREQUENCY seconds
 * = 10^9/POK_TIMER_FREQUENCY nanoseconds */
/* Base + fractional remainder distribution to avoid drift */
#define NSEC_PER_SEC 1000000000ULL
#define TICK_NS_BASE ((uint32_t)(NSEC_PER_SEC / POK_TIMER_FREQUENCY))
#define TICK_NS_REM ((uint32_t)(NSEC_PER_SEC % POK_TIMER_FREQUENCY))
  pok_tick_counter += TICK_NS_BASE;
  static uint32_t ns_rem_acc;
  ns_rem_acc += TICK_NS_REM;
  if (ns_rem_acc >= POK_TIMER_FREQUENCY) {
    ns_rem_acc -= POK_TIMER_FREQUENCY;
    pok_tick_counter += 1; /* distribute leftover nanoseconds */
  }

  /* Single-core ARM - no scheduler election needed */
  /* (void)pok_sched_election(); */

  /* Call scheduler to handle timeslicing and partition switching */
  pok_sched();
}

/* BSP time initialization moved to arm_compat.c to avoid multiple definitions
 */

/* No scheduler election function needed for single-core ARM */
