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

#ifndef __POK_STM32F4_CLOCK_CONFIG_H__
#define __POK_STM32F4_CLOCK_CONFIG_H__

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
#define HSE_FREQ_HZ 8000000 /* 8MHz crystal on STM32F4DISCOVERY */
#endif

/* PLL Configuration Constants */
#define PLL_M 8   /* HSE/8 = 1MHz */
#define PLL_N 336 /* 1MHz * 336 = 336MHz */
#define PLL_P 2   /* 336MHz / 2 = 168MHz SYSCLK */
#define PLL_Q 7   /* 336MHz / 7 = 48MHz for USB */

/* PLL parameter validation per RM0090 */
#define VCO_IN_FREQ (HSE_FREQ_HZ / PLL_M)   /* Input to VCO after M divider */
#define VCO_OUT_FREQ (VCO_IN_FREQ * PLL_N)  /* VCO output frequency */
#define PLL_USB_FREQ (VCO_OUT_FREQ / PLL_Q) /* USB clock frequency */

/* Compile-time validation of PLL parameters */
#if PLL_M < 2 || PLL_M > 63
#error "PLL_M must be between 2 and 63"
#endif
#if PLL_N < 50 || PLL_N > 432
#error "PLL_N must be between 50 and 432"
#endif
#if PLL_P != 2 && PLL_P != 4 && PLL_P != 6 && PLL_P != 8
#error "PLL_P must be 2, 4, 6, or 8"
#endif
#if PLL_Q < 2 || PLL_Q > 15
#error "PLL_Q must be between 2 and 15"
#endif
#if VCO_IN_FREQ < 1000000 || VCO_IN_FREQ > 2000000
#error "VCO input frequency must be between 1-2 MHz"
#endif
#if VCO_OUT_FREQ < 100000000 || VCO_OUT_FREQ > 432000000
#error "VCO output frequency must be between 100-432 MHz"
#endif
#if PLL_USB_FREQ != 48000000
#error "USB clock must be exactly 48 MHz for USB compliance"
#endif

/* PLL register bit positions and values */
/* Use CMSIS-provided bit positions when available; fall back otherwise */
#ifndef RCC_PLLCFGR_PLLQ_Pos
#define RCC_PLLCFGR_PLLQ_Pos 24
#endif
#ifndef RCC_PLLCFGR_PLLP_Pos
#define RCC_PLLCFGR_PLLP_Pos 16
#endif
#ifndef RCC_PLLCFGR_PLLN_Pos
#define RCC_PLLCFGR_PLLN_Pos 6
#endif
#ifndef RCC_PLLCFGR_PLLM_Pos
#define RCC_PLLCFGR_PLLM_Pos 0
#endif
#ifndef RCC_PLLCFGR_PLLSRC_Pos
#define RCC_PLLCFGR_PLLSRC_Pos 22
#endif
/* Convert PLL_P value to register encoding (0=/2, 1=/4, 2=/6, 3=/8) */
/* Compile-time validation: PLL_P must be even and in valid range */
_Static_assert(
    PLL_P == 2 || PLL_P == 4 || PLL_P == 6 || PLL_P == 8,
    "PLL_P must be 2, 4, 6, or 8 for valid STM32F4 PLL configuration");
_Static_assert(
    (PLL_P % 2) == 0,
    "PLL_P must be divisible by 2 to prevent truncation in PLL_P_REG_VALUE");

#define PLL_P_REG_VALUE ((PLL_P / 2) - 1)

/* System clock frequencies after PLL configuration */
#define SYSCLK_FREQ_HZ                                                         \
  ((HSE_FREQ_HZ / PLL_M) * PLL_N /                                             \
   PLL_P) /* Main system clock calculated from PLL */

/* Bus prescaler definitions */
#define AHB_PRESCALER 1  /* AHB Prescaler from SYSCLK */
#define APB1_PRESCALER 4 /* APB1 Prescaler from SYSCLK */
#define APB2_PRESCALER 2 /* APB2 Prescaler from SYSCLK */

/* Bus clock frequencies - derived from prescalers */
#define AHB_FREQ_HZ                                                            \
  (SYSCLK_FREQ_HZ / AHB_PRESCALER) /* AHB bus clock (HCLK)                     \
                                    */
#define APB1_FREQ_HZ                                                           \
  (AHB_FREQ_HZ / APB1_PRESCALER) /* APB1 bus clock (PCLK1) - max 42MHz */
#define APB2_FREQ_HZ                                                           \
  (AHB_FREQ_HZ / APB2_PRESCALER) /* APB2 bus clock (PCLK2) - max 84MHz */

/* Timer clock frequencies - correct calculation based on actual prescaler
 * values STM32 rule: Timer clock = APBx clock * 2 when APB prescaler > 1,
 * otherwise APBx clock * 1 Current config: APB1 prescaler = /4 (>1), APB2
 * prescaler = /2 (>1)
 */

/* Calculate timer frequencies based on actual prescaler values */
#define APB1_TIMER_FREQ_HZ                                                     \
  ((APB1_PRESCALER > 1) ? (APB1_FREQ_HZ * 2)                                   \
                        : APB1_FREQ_HZ) /* 84MHz when prescaler=4 */
#define APB2_TIMER_FREQ_HZ                                                     \
  ((APB2_PRESCALER > 1) ? (APB2_FREQ_HZ * 2)                                   \
                        : APB2_FREQ_HZ) /* 168MHz when prescaler=2 */

/* SysTick uses CPU clock (HCLK) when CLKSOURCE=CPU */
#define SYSTICK_FREQ_HZ AHB_FREQ_HZ

/* USB clock frequency (must be 48MHz) - calculated from PLL_Q */
/* USB clock frequency (must be 48MHz) - calculated from PLL_Q */
#define USB_FREQ_HZ ((HSE_FREQ_HZ / PLL_M) * PLL_N / PLL_Q)
_Static_assert(USB_FREQ_HZ == 48000000,
               "USB clock must be exactly 48MHz for proper operation");

/* Flash latency for 168MHz operation at 3.3V */
#define FLASH_LATENCY 5 /* 5 wait states for 150-168MHz */

#endif /* !__POK_STM32F4_CLOCK_CONFIG_H__ */
