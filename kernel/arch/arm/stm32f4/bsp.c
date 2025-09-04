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

/* Forward declarations for STM32F4 specific functions */
pok_ret_t pok_cons_init(void);
pok_ret_t pok_timer_init(void);

/* STM32F4 specific defines */
#define STM32F4_FLASH_BASE    0x08000000
#define STM32F4_SRAM_BASE     0x20000000
#define STM32F4_SRAM_SIZE     0x20000    /* 128KB */

/* Memory layout for STM32F4 */
#define KERNEL_MEMORY_BASE    STM32F4_SRAM_BASE
#define KERNEL_MEMORY_SIZE    0x8000     /* 32KB for kernel */
#define USER_MEMORY_BASE      (STM32F4_SRAM_BASE + KERNEL_MEMORY_SIZE)
#define USER_MEMORY_SIZE      (STM32F4_SRAM_SIZE - KERNEL_MEMORY_SIZE)

/* Memory alignment constants */
#define MEMORY_ALIGNMENT 8
#define MEMORY_ALIGNMENT_MASK 7

/* Simple kernel memory allocator - allocates from kernel space for stacks, etc. */
static uint32_t current_alloc_addr = KERNEL_MEMORY_BASE;

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
  /* TODO: Configure STM32F4 PLL, HSE/HSI, and peripheral clocks for optimal performance */
  
  /* Initialize console */
  ret = pok_cons_init();
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: Console initialization failed: %d\n", ret);
#endif
    return (ret);
  }
  
  /* Initialize timer */
  ret = pok_timer_init();
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: Timer initialization failed: %d\n", ret);
#endif
    return (ret);
  }
  
  return (POK_ERRNO_OK);
}

void *pok_bsp_mem_alloc(size_t size) {
  void *ret;
  
  if (size == 0) {
    return (NULL);
  }
  
  /* Align to 8-byte boundary */
  size = (size + MEMORY_ALIGNMENT_MASK) & ~MEMORY_ALIGNMENT_MASK;
  
  /* Check if we have enough kernel memory remaining - use subtraction to prevent overflow */
  uint32_t kernel_end = KERNEL_MEMORY_BASE + KERNEL_MEMORY_SIZE;
  
  /* Additional sanity check: ensure current_alloc_addr is within valid kernel range */
  if (current_alloc_addr < KERNEL_MEMORY_BASE || current_alloc_addr > kernel_end) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: Kernel allocator corrupted. current_alloc_addr=0x%x\n", current_alloc_addr);
#endif
    return (NULL);
  }
  
  if (size > (kernel_end - current_alloc_addr)) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: Kernel memory exhausted. Requested: %u, Available: %u\n",
           size, (KERNEL_MEMORY_BASE + KERNEL_MEMORY_SIZE) - current_alloc_addr);
#endif
    return (NULL);
  }
  
  ret = (void *)current_alloc_addr;
  current_alloc_addr += size;
  
#ifdef POK_NEEDS_DEBUG
  printf("Allocated %u bytes at 0x%x (kernel space)\n", size, (uint32_t)ret);
#endif
  
  return (ret);
}

inline uint32_t pok_bsp_mem_base(void) {
  return (USER_MEMORY_BASE);
}

inline uint32_t pok_bsp_mem_size(void) {
  return (USER_MEMORY_SIZE);
}

inline uint32_t pok_bsp_kernel_base(void) {
  return (KERNEL_MEMORY_BASE);
}

inline uint32_t pok_bsp_kernel_size(void) {
  return (KERNEL_MEMORY_SIZE);
}