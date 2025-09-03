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

#include <errno.h>
#include <core/time.h>
#include "../nvic.h"

/* SysTick registers */
#define SYSTICK_BASE      0xE000E010
#define SYSTICK_CSR       (*((volatile uint32_t *)(SYSTICK_BASE + 0x00)))
#define SYSTICK_RVR       (*((volatile uint32_t *)(SYSTICK_BASE + 0x04)))
#define SYSTICK_CVR       (*((volatile uint32_t *)(SYSTICK_BASE + 0x08)))

/* SysTick Control and Status Register bits */
#define SYSTICK_CSR_ENABLE    (1 << 0)
#define SYSTICK_CSR_TICKINT   (1 << 1)
#define SYSTICK_CSR_CLKSOURCE (1 << 2)

/* System clock frequency (Hz) - STM32F4 default */
#define SYSTEM_CLOCK_HZ   16000000

/* Timer tick frequency (100 Hz = 10ms ticks) */
#define TIMER_TICK_HZ     100
#define TIMER_RELOAD_VAL  (SYSTEM_CLOCK_HZ / TIMER_TICK_HZ)

pok_ret_t pok_timer_init(void) {
  /* Disable SysTick */
  SYSTICK_CSR = 0;
  
  /* Set reload value for desired tick rate */
  SYSTICK_RVR = TIMER_RELOAD_VAL - 1;
  
  /* Clear current value */
  SYSTICK_CVR = 0;
  
  /* Configure SysTick: enable, interrupt, use processor clock */
  SYSTICK_CSR = SYSTICK_CSR_ENABLE | SYSTICK_CSR_TICKINT | SYSTICK_CSR_CLKSOURCE;
  
  return (POK_ERRNO_OK);
}

void pok_timer_handler(void) {
  /* Clear SysTick interrupt flag (automatically cleared by reading CSR) */
  (void)SYSTICK_CSR;
  
  /* Update POK system time */
  pok_tick_counter++;
  
  /* Trigger scheduler if needed */
  extern void pok_sched_end_period(void);
  pok_sched_end_period();
}