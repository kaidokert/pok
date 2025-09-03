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
#include "test_deployment.h"
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

/* Timer tick frequency (100 Hz = 10ms ticks) */
#define TIMER_TICK_HZ 100
#define TIMER_RELOAD_VAL (SYSTICK_FREQ_HZ / TIMER_TICK_HZ)

/* SysTick reload register is 24-bit */
#define SYSTICK_MAX_RELOAD 0xFFFFFF

pok_ret_t pok_timer_init(void) {
  /* Validate SysTick reload value doesn't exceed 24-bit limit */
  if (TIMER_RELOAD_VAL > SYSTICK_MAX_RELOAD) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: SysTick reload value %u exceeds 24-bit limit %u\n",
           TIMER_RELOAD_VAL, SYSTICK_MAX_RELOAD);
    printf("Consider reducing SYSTEM_CLOCK_HZ or increasing TIMER_TICK_HZ\n");
#endif
    return POK_ERRNO_EINVAL;
  }

#ifdef POK_NEEDS_DEBUG
  printf("SysTick: %u Hz system clock, %u Hz tick rate, reload = %u\n",
         SYSTICK_FREQ_HZ, TIMER_TICK_HZ, TIMER_RELOAD_VAL);
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

  /* Update POK system time */
  pok_tick_counter++;

  /* Trigger scheduler if needed */
  (void)pok_sched_end_period();
}
