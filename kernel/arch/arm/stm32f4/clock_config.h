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

#ifndef __STM32F4_CLOCK_CONFIG_H__
#define __STM32F4_CLOCK_CONFIG_H__

/**
 * \file    clock_config.h
 * \brief   STM32F4 clock configuration constants
 * \author  POK team
 * 
 * STM32F407VG typical clock configuration with 8MHz HSE crystal:
 * - HSE: 8MHz (external crystal)
 * - PLL_M: 8 (HSE/8 = 1MHz)
 * - PLL_N: 336 (1MHz * 336 = 336MHz) 
 * - PLL_P: 2 (336MHz / 2 = 168MHz SYSCLK)
 * - PLL_Q: 7 (336MHz / 7 = 48MHz for USB)
 * - AHB Prescaler: /1 (168MHz)
 * - APB1 Prescaler: /4 (42MHz, max 42MHz)
 * - APB2 Prescaler: /2 (84MHz, max 84MHz)
 */

/* High-Speed External oscillator frequency */
#ifndef HSE_FREQ_HZ
#define HSE_FREQ_HZ         8000000   /* 8MHz crystal on STM32F4DISCOVERY */
#endif

/* System clock frequencies after PLL configuration */
#define SYSCLK_FREQ_HZ      168000000  /* Main system clock */
#define AHB_FREQ_HZ         168000000  /* AHB bus clock (HCLK) */
#define APB1_FREQ_HZ        42000000   /* APB1 bus clock (PCLK1) - max 42MHz */
#define APB2_FREQ_HZ        84000000   /* APB2 bus clock (PCLK2) - max 84MHz */

/* Timer clock frequencies (APBx clocks x2 when prescaler > 1) */
#define APB1_TIMER_FREQ_HZ  (APB1_FREQ_HZ * 2)  /* 84MHz */
#define APB2_TIMER_FREQ_HZ  (APB2_FREQ_HZ * 2)  /* 168MHz */

/* SysTick uses SYSCLK by default */
#define SYSTICK_FREQ_HZ     SYSCLK_FREQ_HZ

/* USB clock frequency (must be 48MHz) */
#define USB_FREQ_HZ         48000000

/* Flash latency for 168MHz operation at 3.3V */
#define FLASH_LATENCY       5  /* 5 wait states for 150-168MHz */

#endif /* !__STM32F4_CLOCK_CONFIG_H__ */
