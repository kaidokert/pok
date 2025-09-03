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

#include <errno.h>
#include <bsp.h>
#include <libc.h>

/* STM32F4 specific defines */
#define STM32F4_FLASH_BASE    0x08000000
#define STM32F4_SRAM_BASE     0x20000000
#define STM32F4_SRAM_SIZE     0x20000    /* 128KB */

/* Memory layout for STM32F4 */
#define KERNEL_MEMORY_BASE    STM32F4_SRAM_BASE
#define KERNEL_MEMORY_SIZE    0x8000     /* 32KB for kernel */
#define USER_MEMORY_BASE      (STM32F4_SRAM_BASE + KERNEL_MEMORY_SIZE)
#define USER_MEMORY_SIZE      (STM32F4_SRAM_SIZE - KERNEL_MEMORY_SIZE)

/* Simple memory allocator */
static uint32_t current_alloc_addr = KERNEL_MEMORY_BASE;

void pok_bsp_init(void) {
  /* Initialize system clocks */
  /* In a real implementation, this would configure the STM32F4 clocks */
  
  /* Initialize console */
  pok_cons_init();
  
  /* Initialize timer */
  pok_timer_init();
}

char *pok_bsp_mem_alloc(uint32_t size) {
  char *ret;
  
  /* Align to 8-byte boundary */
  size = (size + 7) & ~7;
  
  /* Check if we have enough memory */
  if (current_alloc_addr + size > USER_MEMORY_BASE) {
    return NULL;
  }
  
  ret = (char *)current_alloc_addr;
  current_alloc_addr += size;
  
  return ret;
}

uint32_t pok_bsp_mem_base(void) {
  return USER_MEMORY_BASE;
}

uint32_t pok_bsp_mem_size(void) {
  return USER_MEMORY_SIZE;
}