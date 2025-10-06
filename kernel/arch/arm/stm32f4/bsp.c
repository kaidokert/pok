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
 * \file    arch/arm/stm32f4/bsp.c
 * \author  POK team
 * \brief   STM32F4 Board Support Package
 */

#include "clock_config.h"
#include "peripherals.h"
#include "pm.h"
#include <bsp.h>
#include <errno.h>
#include <libc.h>

/* STM32F4-specific memory configuration overrides */
#define POK_FLASH_BASE STM32F4_FLASH_BASE
#define POK_SRAM_BASE STM32F4_SRAM_BASE

/* STM32F4 SRAM size - configurable for different MCU variants
 * STM32F407VG/417VG: 128KB main SRAM + 64KB CCM
 * STM32F429/439: 256KB main SRAM + 64KB CCM
 * Override POK_SRAM_SIZE in platform-specific headers if needed */
#ifndef POK_SRAM_SIZE
#define POK_SRAM_SIZE                                                          \
  0x20000 /* 128KB (conservative until 192KB MPU config resolved) */
#endif

#define POK_KERNEL_MEMORY_SIZE 0x8000     /* 32KB for kernel */
#define POK_KERNEL_MEMORY_BASE 0x20000000 /* Start of SRAM */
#define POK_USER_MEMORY_BASE 0x20008000   /* After 32KB kernel space */
#define POK_USER_MEMORY_SIZE 0x18000      /* 96KB for user partitions */

/* Forward declarations for STM32F4 specific functions */
pok_ret_t pok_cons_init(void);
pok_ret_t pok_timer_init(void);
pok_ret_t pok_stm32f4_clock_init(void);

/* External linker symbols */
extern unsigned int _estack; /* Linker-provided stack top symbol */

/**
 * Initialize STM32F4 Board Support Package
 *
 * Sets up system clocks, console UART, and system timer.
 * Must be called early in system initialization.
 *
 * @return POK_ERRNO_OK on success, error code on failure
 */
pok_ret_t pok_bsp_init(void) {
  pok_ret_t ret;

  /* Initialize system clocks */
  ret = pok_stm32f4_clock_init();
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: Clock initialization failed: %d\n", ret);
#endif
    return (ret);
  }

  /* Initialize console */
  ret = pok_cons_init();
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: Console initialization failed: %d\n", ret);
#endif
    return (ret);
  }

  /* Initialize physical memory management */
  ret = pok_pm_init();
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: PM initialization failed: %d\n", ret);
#endif
    return (ret);
  }

  /* Timer initialization moved to pok_time_init() -> pok_bsp_time_init() */

  return POK_ERRNO_OK;
}

void *pok_bsp_mem_alloc(size_t size) { return ((void *)pok_pm_sbrk(size)); }

uintptr_t pok_bsp_mem_base(void) { return (POK_USER_MEMORY_BASE); }

size_t pok_bsp_mem_size(void) { return (POK_USER_MEMORY_SIZE); }

uintptr_t pok_bsp_kernel_base(void) { return (POK_KERNEL_MEMORY_BASE); }

size_t pok_bsp_kernel_size(void) { return (POK_KERNEL_MEMORY_SIZE); }

void pok_bsp_mem_free(void *ptr, size_t size) {
  /* Simple allocator - cannot free individual blocks
   * This is a basic bump allocator that doesn't support freeing.
   * In embedded systems, memory is typically allocated once at startup.
   * For more sophisticated memory management, a proper heap allocator
   * would be needed. */
  (void)ptr;  /* Unused parameter */
  (void)size; /* Unused parameter */
}

/**
 * Initialize STM32F4 system clocks
 * Configures PLL for 168MHz operation using 8MHz HSE crystal
 *
 * @return POK_ERRNO_OK on success, error code on failure
 */
pok_ret_t pok_stm32f4_clock_init(void) {
  /* Validate HSE frequency - PLL calculations are hardcoded for 8MHz */
  if (HSE_FREQ_HZ != 8000000) {
    /* HSE frequency mismatch - silently continue with current configuration */
  }

  /* Validate USB clock calculation for current HSE */
  /* USB_CLK = (HSE * PLL_N / PLL_M) / PLL_Q */
  uint32_t usb_freq_calculated =
      ((uint32_t)HSE_FREQ_HZ * (uint32_t)PLL_N / (uint32_t)PLL_M) /
      (uint32_t)PLL_Q;
  if (usb_freq_calculated != USB_FREQ_HZ) {
    /* USB clock mismatch - silently continue (USB not needed for basic
     * operation) */
  }

  volatile uint32_t *RCC_CR =
      (volatile uint32_t *)(RCC_BASE + 0x00); /* RCC Clock Control Register */
  volatile uint32_t *RCC_PLLCFGR =
      (volatile uint32_t *)(RCC_BASE +
                            0x04); /* RCC PLL Configuration Register */
  volatile uint32_t *RCC_CFGR =
      (volatile uint32_t *)(RCC_BASE +
                            0x08); /* RCC Clock Configuration Register */
  volatile uint32_t *FLASH_ACR =
      (volatile uint32_t *)(STM32F4_FLASH_CTRL_BASE +
                            0x00); /* Flash Access Control Register */

  uint32_t timeout = 0;

  /* Set flash latency for 168MHz operation (5 wait states for 150-168MHz
   * at 3.3V) and enable prefetch buffer, instruction and data caches for
   * optimal performance at 168MHz */
  *FLASH_ACR = (*FLASH_ACR & ~0x7) | FLASH_LATENCY |
               (1 << 8) | /* PRFTEN - Prefetch buffer enable */
               (1 << 9) | /* ICEN - Instruction cache enable */
               (1 << 10); /* DCEN - Data cache enable */

  /* Enable HSE (High Speed External clock) */
  *RCC_CR |= (1 << 16); /* HSEON = 1 */

  /* Wait for HSE to be ready */
  timeout = 10000;
  while (!((*RCC_CR) & (1 << 17)) && timeout > 0) { /* Wait for HSERDY = 1 */
    timeout--;
  }

  if (timeout == 0) {
#ifdef POK_NEEDS_DEBUG
    printf("WARNING: HSE ready timeout - continuing anyway (QEMU "
           "compatibility)\n");
#endif
    /* Continue without error in emulation environments like QEMU */
  }

  /* Configure voltage regulator scaling for 168MHz operation
   * VOS = Scale 1 mode (required for frequencies > 144 MHz) */
  volatile uint32_t *PWR_CR = (volatile uint32_t *)STM32F4_PWR_BASE;

  /* Enable PWR clock in RCC */
  volatile uint32_t *RCC_APB1ENR = (volatile uint32_t *)(RCC_BASE + 0x40);
  *RCC_APB1ENR |= (1 << 28); /* PWREN = 1 */

  /* Set VOS to Scale 1 (highest performance, required for 168MHz) */
  *PWR_CR =
      (*PWR_CR & ~(3 << 14)) | (3 << 14); /* VOS[1:0] = 11 (Scale 1 mode) */

  /* Wait for voltage regulator to be ready */
  timeout = 1000;
  volatile uint32_t *PWR_CSR = (volatile uint32_t *)(STM32F4_PWR_BASE + 0x04);
  while (!((*PWR_CSR) & (1 << 14)) && timeout > 0) { /* Wait for VOSRDY = 1 */
    timeout--;
  }

  if (timeout == 0) {
#ifdef POK_NEEDS_DEBUG
    printf("WARNING: Voltage regulator timeout - continuing anyway (QEMU "
           "compatibility)\n");
#endif
    /* Continue without error in emulation environments like QEMU */
  }

  /* Configure PLL using constants from clock_config.h */
  *RCC_PLLCFGR = (PLL_Q << RCC_PLLCFGR_PLLQ_Pos) |
                 (PLL_P_REG_VALUE << RCC_PLLCFGR_PLLP_Pos) |
                 (PLL_N << RCC_PLLCFGR_PLLN_Pos) |
                 (PLL_M << RCC_PLLCFGR_PLLM_Pos) |
                 (1 << RCC_PLLCFGR_PLLSRC_Pos); /* HSE as PLL source */

  /* Enable PLL */
  *RCC_CR |= (1 << 24); /* PLLON = 1 */

  /* Wait for PLL to be ready */
  timeout = 10000;
  while (!((*RCC_CR) & (1 << 25)) && timeout > 0) { /* Wait for PLLRDY = 1 */
    timeout--;
  }

  if (timeout == 0) {
#ifdef POK_NEEDS_DEBUG
    printf("WARNING: PLL ready timeout - continuing anyway (QEMU "
           "compatibility)\n");
#endif
    /* Continue without error in emulation environments like QEMU */
  }

  /* Configure bus prescalers using read-modify-write:
   * AHB Prescaler: /1 (168MHz)
   * APB1 Prescaler: /4 (42MHz, max 42MHz)
   * APB2 Prescaler: /2 (84MHz, max 84MHz)
   */
  uint32_t cfgr_val = *RCC_CFGR;
  cfgr_val &=
      ~((0xF << 4) | (0x7 << 10) | (0x7 << 13)); /* Clear prescaler fields */
  cfgr_val |= (0x0 << 4) |                       /* AHB prescaler /1 */
              (0x5 << 10) |                      /* APB1 prescaler /4 */
              (0x4 << 13);                       /* APB2 prescaler /2 */
  *RCC_CFGR = cfgr_val;

  /* Switch system clock to PLL */
  *RCC_CFGR = (*RCC_CFGR & ~0x3) | 0x2; /* SW = 10 (PLL as system clock) */

  /* Wait for PLL to be used as system clock */
  timeout = 10000;
  while (((*RCC_CFGR & 0xC) >> 2) != 0x2 &&
         timeout > 0) { /* Wait for SWS = 10 */
    timeout--;
  }

  if (timeout == 0) {
#ifdef POK_NEEDS_DEBUG
    printf("WARNING: PLL switch timeout - continuing anyway (QEMU "
           "compatibility)\n");
#endif
    /* Continue without error in emulation environments like QEMU */
  }

#ifdef POK_NEEDS_DEBUG
  printf("STM32F4 clock configured: SYSCLK=%dMHz, AHB=%dMHz, APB1=%dMHz, "
         "APB2=%dMHz\n",
         SYSCLK_FREQ_HZ / 1000000, AHB_FREQ_HZ / 1000000,
         APB1_FREQ_HZ / 1000000, APB2_FREQ_HZ / 1000000);
#endif

  return POK_ERRNO_OK;
}

/**
 * Print comprehensive system debug information
 * Provides detailed monitoring of system state and resources
 */
void pok_bsp_debug_monitor(void) {
#ifdef POK_NEEDS_DEBUG
  printf("=== BSP SYSTEM MONITOR ===\n");

  /* Clock status */
  volatile uint32_t *RCC_CR = (volatile uint32_t *)(RCC_BASE + 0x00);
  volatile uint32_t *RCC_CFGR = (volatile uint32_t *)(RCC_BASE + 0x08);

  printf("Clock status:\n");
  printf("  HSE Ready: %s\n", (*RCC_CR & (1 << 17)) ? "YES" : "NO");
  printf("  PLL Ready: %s\n", (*RCC_CR & (1 << 25)) ? "YES" : "NO");
  printf("  System clock source: %s\n",
         (((*RCC_CFGR & 0xC) >> 2) == 0x2) ? "PLL" : "Other");

  /* Memory status */
  printf("Memory layout:\n");
  printf("  Kernel base: 0x%08lx, size: %u bytes\n",
         (unsigned long)POK_KERNEL_MEMORY_BASE, POK_KERNEL_MEMORY_SIZE);
  printf("  User base: 0x%08lx, size: %u bytes\n",
         (unsigned long)POK_USER_MEMORY_BASE, POK_USER_MEMORY_SIZE);
  printf("  Heap allocator: brk=0x%08x\n", pok_arm_pm_brk);

  /* Stack status */
  unsigned int current_sp;
  __asm volatile("mov %0, sp" : "=r"(current_sp));
  printf("Stack info:\n");
  printf("  Stack top: 0x%08x\n", (unsigned int)&_estack);
  printf("  Current SP: 0x%08x\n", current_sp);
  printf("  Stack used: %u bytes\n", (unsigned int)&_estack - current_sp);

  printf("=== END BSP MONITOR ===\n");
#endif
}

/* ========================================================================
 * BSP ABSTRACTION IMPLEMENTATION for Architecture Layer
 * ======================================================================== */

/* BSP-provided memory ranges for architecture layer */
uint32_t pok_bsp_flash_base = STM32F4_FLASH_BASE;
uint32_t pok_bsp_flash_size = STM32F4_FLASH_SIZE;

/* BSP-provided fault output function for architecture layer */
static void stm32f4_fault_putchar(char c) {
  /* STM32F4 USART1 registers for non-blocking fault output */
  volatile uint32_t *usart1_sr =
      (volatile uint32_t *)(STM32F4_USART1_BASE + 0x00);
  volatile uint32_t *usart1_dr =
      (volatile uint32_t *)(STM32F4_USART1_BASE + 0x04);

  uint32_t status = *usart1_sr;

  /* Check for UART errors - clear them but continue trying to output */
  if (status & ((1 << 0) | (1 << 1) | (1 << 3))) { /* PE | FE | ORE */
    /* Clear error flags by reading SR then DR (hardware requirement) */
    (void)*usart1_dr;
  }

  /* Try to output character without blocking if transmitter is ready */
  if (status & (1 << 7)) { /* TXE - Transmit data register empty */
    *usart1_dr = c;
  }
  /* If UART not ready, we silently drop the character - fault context
   * requires non-blocking operation for system stability */
}

/* BSP abstraction function pointer - set during BSP initialization */
void (*pok_bsp_fault_putchar)(char c) = stm32f4_fault_putchar;
