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
#include <types.h> /* Ensure int64_t is available */

/* ARM Cortex-M is single-processor - define required configuration */
#ifndef POK_CONFIG_NB_PROCESSORS
#define POK_CONFIG_NB_PROCESSORS 1
#endif

#include <core/sched.h>
#include <core/time.h>
#include <errno.h>
#include <libc.h>

/* SysTick registers */
/* SYSTICK_BASE now defined in peripherals.h */
#define SYSTICK_CSR (*((volatile uint32_t *)(SYSTICK_BASE + 0x00)))
#define SYSTICK_RVR (*((volatile uint32_t *)(SYSTICK_BASE + 0x04)))
#define SYSTICK_CVR (*((volatile uint32_t *)(SYSTICK_BASE + 0x08)))

/* SysTick Control and Status Register bits */
#define SYSTICK_CSR_ENABLE (1 << 0)
#define SYSTICK_CSR_TICKINT (1 << 1)
#define SYSTICK_CSR_CLKSOURCE (1 << 2)

/* Compile-time checks to prevent divide-by-zero errors */
#if !defined(POK_TIMER_FREQUENCY) || (POK_TIMER_FREQUENCY == 0)
#error "POK_TIMER_FREQUENCY must be defined and non-zero"
#endif

#if !defined(SYSTICK_FREQ_HZ) || (SYSTICK_FREQ_HZ == 0)
#error "SYSTICK_FREQ_HZ must be defined and non-zero"
#endif

/* Timer frequency from POK core (100kHz for proper timing consistency) */
#define TIMER_RELOAD_VAL (SYSTICK_FREQ_HZ / POK_TIMER_FREQUENCY)

/* SysTick reload register is 24-bit */
#define SYSTICK_MAX_RELOAD 0xFFFFFF
#define SYSTICK_MIN_RELOAD                                                     \
  100 /* Minimum reasonable reload value for proper timing */

pok_ret_t pok_timer_init(void) {
  /* Enhanced SysTick reload validation for all clock configurations */

  /* Check for invalid system frequency */
  if (SYSTICK_FREQ_HZ == 0) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: SYSTICK_FREQ_HZ cannot be zero\n");
#endif
    return POK_ERRNO_EINVAL;
  }

  /* CRITICAL: Check for timer configuration that would cause divide-by-zero */
  if (POK_TIMER_FREQUENCY > SYSTICK_FREQ_HZ) {
#ifdef POK_NEEDS_DEBUG
    printf(
        "ERROR: POK_TIMER_FREQUENCY (%u Hz) exceeds SYSTICK_FREQ_HZ (%u Hz)\n",
        POK_TIMER_FREQUENCY, SYSTICK_FREQ_HZ);
    printf("This would cause TIMER_RELOAD_VAL = 0 and subsequent "
           "divide-by-zero\n");
    printf("Reduce POK_TIMER_FREQUENCY or increase SYSTICK_FREQ_HZ\n");
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Additional check: ensure TIMER_RELOAD_VAL is not zero after division */
  if (TIMER_RELOAD_VAL == 0) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: TIMER_RELOAD_VAL computed to zero\n");
    printf("SYSTICK_FREQ_HZ=%u, POK_TIMER_FREQUENCY=%u\n", SYSTICK_FREQ_HZ,
           POK_TIMER_FREQUENCY);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Validate reload value is reasonable (not too small) */
  if (TIMER_RELOAD_VAL < SYSTICK_MIN_RELOAD) {
#ifdef POK_NEEDS_DEBUG
    printf("WARNING: SysTick reload value %u too small (min %u recommended)\n",
           TIMER_RELOAD_VAL, SYSTICK_MIN_RELOAD);
    printf("System freq: %u Hz, POK timer freq: %u Hz - continuing anyway\n",
           SYSTICK_FREQ_HZ, POK_TIMER_FREQUENCY);
#endif
    /* Continue with initialization instead of failing */
  }

  /* Validate SysTick reload value doesn't exceed 24-bit limit */
  if (TIMER_RELOAD_VAL > SYSTICK_MAX_RELOAD) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: SysTick reload value %u exceeds 24-bit limit %u\n",
           TIMER_RELOAD_VAL, SYSTICK_MAX_RELOAD);
    printf("Consider reducing SYSTICK_FREQ_HZ or increasing "
           "POK_TIMER_FREQUENCY\n");
#endif
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
#ifdef POK_NEEDS_DEBUG
    printf("WARNING: Actual timer frequency %u Hz differs from target %u Hz\n",
           actual_freq, POK_TIMER_FREQUENCY);
    printf("Clock configuration may need adjustment\n");
#endif
  }

#ifdef POK_NEEDS_DEBUG
  printf("SysTick: %u Hz system clock, %u Hz timer freq, reload = %u\n",
         SYSTICK_FREQ_HZ, POK_TIMER_FREQUENCY, TIMER_RELOAD_VAL);
#endif

  /* Disable SysTick */
  SYSTICK_CSR = 0;

  /* Set reload value for desired tick rate */
  SYSTICK_RVR = TIMER_RELOAD_VAL - 1;

  /* Clear current value */
  SYSTICK_CVR = 0;

  /* Configure SysTick: enable, interrupt, use processor clock */
  SYSTICK_CSR =
      SYSTICK_CSR_ENABLE | SYSTICK_CSR_TICKINT | SYSTICK_CSR_CLKSOURCE;

  /* Data Synchronization Barrier to ensure SysTick configuration completes */
  __asm volatile("dsb" : : : "memory");
  __asm volatile("isb" : : : "memory");

  return POK_ERRNO_OK;
}

void pok_timer_handler(void) {
  /* Clear SysTick interrupt flag (automatically cleared by reading CSR) */
  (void)SYSTICK_CSR;

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

  /* Trigger scheduler election - timer ticks may require context switch */
  (void)pok_sched_election();
}
