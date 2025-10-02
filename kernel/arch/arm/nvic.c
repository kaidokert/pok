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
 * \file    arch/arm/nvic.c
 * \author  POK team
 * \brief   ARM Cortex-M NVIC (Nested Vectored Interrupt Controller)
 * implementation
 */

/* POK system headers */
#include <errno.h>
#ifdef POK_NEEDS_DEBUG
#include <bsp.h> /* For pok_cons_write in debug builds */
#endif

/* Architecture-specific headers */
#include "arch.h"
/* Conditional includes - STM32F4 specific for full implementation */
#ifdef POK_NEEDS_STM32F4_PERIPHERALS
#include "cortex_m_config.h"
#include "memory_config.h"
#include "stm32f4/peripherals.h"
#endif
#include "nvic.h"

/* ARM Cortex-M intrinsics */
#ifdef __has_include
#if __has_include(<cmsis_gcc.h>)
#include <cmsis_gcc.h>
#define HAVE_CMSIS_GCC 1
#endif
#endif

#ifndef HAVE_CMSIS_GCC
/* Fallback intrinsics for builds without CMSIS */
static inline void __disable_irq(void) {
  __asm volatile("cpsid i" : : : "memory");
}
static inline uint32_t __get_PRIMASK(void) {
  uint32_t result;
  __asm volatile("mrs %0, PRIMASK" : "=r"(result) : : "memory");
  return result;
}
static inline void __set_PRIMASK(uint32_t priMask) {
  __asm volatile("msr PRIMASK, %0" : : "r"(priMask) : "memory");
}
#endif

/* External vector table (defined in startup code) */
/* Note: We get the ROM vector table location from VTOR instead of assuming
 * a fixed location, making this more robust across different memory layouts */
extern unsigned int _estack;

/* Default handler from startup.S */
extern void Default_Handler(void);

/* RAM-based vector table for runtime handler updates */
#define NVIC_VECTOR_COUNT CORTEX_M_NVIC_VECTOR_COUNT
#define NVIC_VECTOR_TABLE_SIZE CORTEX_M_NVIC_VECTOR_TABLE_SIZE
#define NVIC_VECTOR_TABLE_ALIGNMENT CORTEX_M_NVIC_VECTOR_TABLE_ALIGNMENT
static vector_table_entry_t ram_vector_table[NVIC_VECTOR_COUNT]
    __attribute__((aligned(NVIC_VECTOR_TABLE_ALIGNMENT)));
static uint8_t vector_table_relocated = 0;

/* Default handlers */
static void pok_nvic_default_handler(void) {
  /* Default handler - log error and halt system safely
   * Avoid WFI as it may create unrecoverable state if no other interrupts occur
   */
#ifdef POK_NEEDS_DEBUG
  pok_cons_write("FATAL: Unhandled interrupt/exception occurred\n", 49);
#endif

  /* Disable interrupts and halt */
  __disable_irq();
  while (1) {
    /* Busy wait instead of WFI to ensure system remains debuggable */
    __asm volatile("nop");
  }
}

/**
 * Relocate vector table from FLASH to RAM for runtime handler updates
 */
static pok_ret_t pok_nvic_relocate_vector_table(void) {
  if (vector_table_relocated) {
    return POK_ERRNO_OK; /* Already relocated */
  }

  /* Read VTOR directly as volatile */
  uint32_t rom_table_addr = *SCB_VTOR;

  /* Validate ROM table address is in valid memory region (Flash or SRAM) */
  pok_bool_t in_flash =
      (rom_table_addr >= STM32F4_FLASH_BASE &&
       rom_table_addr < (STM32F4_FLASH_BASE + STM32F4_FLASH_SIZE));
  pok_bool_t in_sram = (rom_table_addr >= POK_SRAM_BASE &&
                        rom_table_addr < (POK_SRAM_BASE + POK_SRAM_SIZE));

  if (!in_flash && !in_sram) {
#ifdef POK_NEEDS_DEBUG
    /* printf("ERROR: ROM vector table at invalid address: 0x%x (outside Flash "
           "0x%x-0x%x or SRAM 0x%x-0x%x)\n",
           (unsigned int)rom_table_addr, (unsigned int)STM32F4_FLASH_BASE,
           (unsigned int)(STM32F4_FLASH_BASE + STM32F4_FLASH_SIZE - 1),
           (unsigned int)POK_SRAM_BASE,
           (unsigned int)(POK_SRAM_BASE + POK_SRAM_SIZE - 1)); */
    pok_cons_write("ERROR: ROM vector table at invalid address\n", 43);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Ensure full vector table fits within the detected memory region */
  uint32_t region_end = in_flash ? (STM32F4_FLASH_BASE + STM32F4_FLASH_SIZE)
                                 : (POK_SRAM_BASE + POK_SRAM_SIZE);
  uint32_t table_end = rom_table_addr + NVIC_VECTOR_TABLE_SIZE;

  /* Check for overflow in table_end calculation and region bounds */
  if (table_end < rom_table_addr || table_end > region_end) {
#ifdef POK_NEEDS_DEBUG
    /* printf("ERROR: ROM vector table (0x%x + %u bytes) extends beyond %s "
           "region (end: 0x%x)\n",
           (unsigned int)rom_table_addr, NVIC_VECTOR_TABLE_SIZE,
           in_flash ? "Flash" : "SRAM", (unsigned int)region_end); */
    pok_cons_write("ERROR: ROM vector table extends beyond region\n", 45);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Validate vector table alignment */
  if ((rom_table_addr & (NVIC_VECTOR_TABLE_ALIGNMENT - 1)) != 0) {
#ifdef POK_NEEDS_DEBUG
    /* printf(
        "ERROR: ROM vector table misaligned: 0x%x (must be %d-byte aligned)\n",
        rom_table_addr, NVIC_VECTOR_TABLE_ALIGNMENT); */
    pok_cons_write("ERROR: ROM vector table misaligned\n", 35);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Copy ROM vector table to RAM using memcpy to avoid strict-aliasing issues
   */
  uint32_t *rom_vector_table_u32 = (uint32_t *)rom_table_addr;
  uint32_t *ram_vector_table_u32 = (uint32_t *)ram_vector_table;

  for (int i = 0; i < NVIC_VECTOR_COUNT; i++) {
    ram_vector_table_u32[i] = rom_vector_table_u32[i];
  }

  /* Ensure MSP initial value points to valid RAM stack top
   * Vector 0 is MSP, not a code address, so no Thumb bit needed */
  ram_vector_table_u32[0] = (uint32_t)&_estack;

  /* Update VTOR register to point to RAM vector table */
  uint32_t ram_table_addr = (uint32_t)ram_vector_table;

  /* Validate alignment (must be next power of 2 of table size) */
  if (ram_table_addr & (NVIC_VECTOR_TABLE_ALIGNMENT - 1)) {
#ifdef POK_NEEDS_DEBUG
    /* printf("ERROR: RAM vector table not properly aligned: 0x%x (required: %d
       " "bytes)\n", (unsigned int)ram_table_addr, NVIC_VECTOR_TABLE_ALIGNMENT);
     */
    pok_cons_write("ERROR: RAM vector table not properly aligned\n", 44);
#endif
    return POK_ERRNO_EFAULT;
  }

  *SCB_VTOR = ram_table_addr;
  vector_table_relocated = 1;
  __asm volatile("dsb" ::: "memory");
  __asm volatile("isb");

#ifdef POK_NEEDS_DEBUG
  /* printf("Vector table relocated to RAM at 0x%x\n",
         (unsigned int)ram_table_addr); */
  pok_cons_write("Vector table relocated to RAM\n", 30);
#endif
  return POK_ERRNO_OK;
}

pok_ret_t pok_nvic_init(void) {
  pok_ret_t ret;

#ifdef POK_NEEDS_DEBUG
  pok_cons_write("pok_nvic_init: ENTRY\n", 21);
#endif

  /* Relocate vector table to RAM for runtime handler updates */
  ret = pok_nvic_relocate_vector_table();
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("pok_nvic_init: relocate FAILED, ret=", 37);
    char hex_buf[3];
    hex_buf[0] = "0123456789ABCDEF"[(ret >> 4) & 0xF];
    hex_buf[1] = "0123456789ABCDEF"[ret & 0xF];
    hex_buf[2] = '\n';
    pok_cons_write(hex_buf, 3);
#endif
    return (ret);
  }

  /* Enable division-by-zero trap to trigger UsageFault */
  *SCB_CCR |= SCB_CCR_DIV_0_TRP;

  /* Enable memory management, bus fault, and usage fault exceptions */
  *SCB_SHCSR |=
      SCB_SHCSR_MEMFAULTENA | SCB_SHCSR_BUSFAULTENA | SCB_SHCSR_USGFAULTENA;

  /* Set fault handlers to high priority for proper error handling */
  ret = pok_nvic_set_priority(EXCEPTION_MEMMANAGE, NVIC_PRIORITY_HIGH);
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("FAILED: set_priority MEMMANAGE\n", 32);
#endif
    return ret;
  }
  ret = pok_nvic_set_priority(EXCEPTION_BUSFAULT, NVIC_PRIORITY_HIGH);
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("FAILED: set_priority BUSFAULT\n", 31);
#endif
    return ret;
  }
  ret = pok_nvic_set_priority(EXCEPTION_USAGEFAULT, NVIC_PRIORITY_HIGH);
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("FAILED: set_priority USAGEFAULT\n", 33);
#endif
    return ret;
  }

  /* Set PendSV and SysTick to lowest priority for context switching */
  ret = pok_nvic_set_priority(EXCEPTION_PENDSV, NVIC_PRIORITY_LOWEST);
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("FAILED: set_priority PENDSV\n", 29);
#endif
    return ret;
  }
  ret = pok_nvic_set_priority(EXCEPTION_SYSTICK, NVIC_PRIORITY_LOWEST);
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("FAILED: set_priority SYSTICK\n", 30);
#endif
    return ret;
  }

  /* Clear all pending interrupts based on actual IRQ count
   * Each ICPR register handles 32 IRQs, so calculate number of registers needed
   * STM32F4 has 82 external IRQs, requiring 3 ICPR registers (0-2) */
  const int external_irqs =
      CORTEX_M_NVIC_VECTOR_COUNT - 16; /* Subtract 16 system vectors */
  const int icpr_regs_needed =
      (external_irqs + 31) / 32; /* Round up to next register */

  for (int i = 0; i < icpr_regs_needed; i++) {
    NVIC_ICPR[i] = 0xFFFFFFFFU;
  }

#ifdef POK_NEEDS_DEBUG
  pok_cons_write("pok_nvic_init: SUCCESS\n", 23);
#endif

  return POK_ERRNO_OK;
}

pok_ret_t pok_nvic_set_handler(uint8_t irq, void (*handler)(void)) {
  /* Bounds checking for interrupt vector numbers */
  if (irq >= NVIC_VECTOR_COUNT) {
#ifdef POK_NEEDS_DEBUG
    /* printf("ERROR: IRQ number %u exceeds maximum vector count (%u)\n", irq,
           NVIC_VECTOR_COUNT); */
    pok_cons_write("ERROR: IRQ number exceeds maximum vector count\n", 46);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Allow NULL handler to restore default handler */
  if (handler == NULL) {
    /* Use Default_Handler from startup.S as the default handler */
    handler = Default_Handler;
  }

  /* Prevent overwriting critical system vectors */
  if (irq == 0 || irq == 1 || irq == 2 || irq == 3) {
    /* Vector 0: Initial Stack Pointer (MSP), Vector 1: Reset Handler,
     * Vector 2: NMI, Vector 3: HardFault - cannot be safely modified at runtime
     */
#ifdef POK_NEEDS_DEBUG
    /* printf("ERROR: Cannot modify critical system vector %u\n", irq); */
    pok_cons_write("ERROR: Cannot modify critical system vector\n", 42);
#endif
    return POK_ERRNO_EINVAL; /* Cannot modify critical system vectors */
  }

  /* Ensure vector table has been relocated to RAM */
  if (!vector_table_relocated) {
    pok_ret_t ret = pok_nvic_relocate_vector_table();
    if (ret != POK_ERRNO_OK) {
      return (ret);
    }
  }

  /* Disable interrupts during handler update to prevent race conditions */
  uint8_t irq_was_enabled = 0;
  uint32_t primask_state = 0;

  if (irq >= EXCEPTION_IRQ0) {
    /* External IRQ - disable specific IRQ */
    uint8_t external_irq = irq - EXCEPTION_IRQ0;
    uint32_t reg_idx = external_irq / 32;
    uint32_t bit_pos = external_irq % 32;
    if (NVIC_ISER[reg_idx] & (1U << bit_pos)) {
      irq_was_enabled = 1;
      NVIC_ICER[reg_idx] = (1U << bit_pos); /* Disable IRQ */
    }
  } else if (irq >= 2 && irq <= 15) {
    /* System exception - NOTE: NMI (2) and HardFault (3) cannot be masked by
     * PRIMASK */
    if (irq == 2 || irq == 3) {
      /* NMI and HardFault are unmaskable - document this limitation */
#ifdef POK_NEEDS_DEBUG
      /* printf("Warning: Setting handler for unmaskable exception %d "
             "(NMI/HardFault)\n",
             irq); */
      pok_cons_write("Warning: Setting handler for unmaskable exception\n", 49);
#endif
    } else {
      /* Other system exceptions can be masked - disable all interrupts to
       * prevent race */
      primask_state = __get_PRIMASK();
      __disable_irq();
    }
  }

  /* Set handler in RAM vector table - handler already validated as non-NULL
   * Convert function pointer to 32-bit address with Thumb bit set
   * ARM Cortex-M requires LSB=1 for Thumb code addresses in vector table */
  uint32_t *ram_table_u32 = (uint32_t *)ram_vector_table;
  ram_table_u32[irq] = ((uint32_t)(uintptr_t)handler) | 1;

  /* Data Synchronization Barrier to ensure vector table update completes */
  __asm volatile("dsb" : : : "memory");
  __asm volatile("isb");

  /* Re-enable interrupts */
  if (irq >= EXCEPTION_IRQ0) {
    /* External IRQ - re-enable specific IRQ if it was enabled */
    if (irq_was_enabled) {
      uint8_t external_irq = irq - EXCEPTION_IRQ0;
      uint32_t reg_idx = external_irq / 32;
      uint32_t bit_pos = external_irq % 32;
      NVIC_ISER[reg_idx] = (1U << bit_pos);
    }
  } else if (irq >= 2 && irq <= 15) {
    /* System exception - restore global interrupt state only if we modified it
     */
    if (irq != 2 && irq != 3) {
      /* Only restore for maskable system exceptions */
      __set_PRIMASK(primask_state);
    }
  }

  return POK_ERRNO_OK;
}

pok_ret_t pok_nvic_enable_irq(uint8_t irq) {
  if (irq < EXCEPTION_IRQ0 || irq >= NVIC_VECTOR_COUNT) {
    return POK_ERRNO_EINVAL;
  }

  uint8_t external_irq = irq - EXCEPTION_IRQ0;
  uint32_t reg_idx = external_irq / 32;
  uint32_t bit_pos = external_irq % 32;

  NVIC_ISER[reg_idx] = (1U << bit_pos);

  /* Data Synchronization Barrier to ensure register write completes */
  __asm volatile("dsb" : : : "memory");

  return POK_ERRNO_OK;
}

pok_ret_t pok_nvic_disable_irq(uint8_t irq) {
  if (irq < EXCEPTION_IRQ0 || irq >= NVIC_VECTOR_COUNT) {
    return POK_ERRNO_EINVAL;
  }

  uint8_t external_irq = irq - EXCEPTION_IRQ0;
  uint32_t reg_idx = external_irq / 32;
  uint32_t bit_pos = external_irq % 32;

  NVIC_ICER[reg_idx] = (1U << bit_pos);

  /* Data Synchronization Barrier to ensure register write completes */
  __asm volatile("dsb" : : : "memory");

  return POK_ERRNO_OK;
}

pok_ret_t pok_nvic_set_priority(uint8_t irq, uint8_t priority) {
  /* Comprehensive priority validation */
  if (irq >= NVIC_VECTOR_COUNT) {
#ifdef POK_NEEDS_DEBUG
    /* printf("ERROR: IRQ %u exceeds vector count (%u)\n", irq,
     * NVIC_VECTOR_COUNT); */
    pok_cons_write("ERROR: IRQ exceeds vector count\n", 32);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Note: priority > NVIC_PRIORITY_LOWEST check removed as it's always false
   * when priority is uint8_t and NVIC_PRIORITY_LOWEST is 255 */

  /* Priority values are 8-bit and will be shifted to match hardware
   * ARM_PRIORITY_BITS The shifting is done when writing to registers (lines
   * ~491, ~495), so no validation needed here. All uint8_t values are valid. */

  /* Check for critical system priorities that shouldn't be modified */
  if ((irq == EXCEPTION_NMI) || (irq == EXCEPTION_HARDFAULT) ||
      (irq == EXCEPTION_RESET) || (irq == 0)) {
#ifdef POK_NEEDS_DEBUG
    /* printf("ERROR: Cannot set priority for critical system exception %u\n",
           irq); */
    pok_cons_write("ERROR: Cannot set priority for critical system exception\n",
                   55);
#endif
    return POK_ERRNO_EINVAL;
  }

  if (irq < EXCEPTION_IRQ0) {
    /* System exception priority */
    volatile uint32_t *shpr_reg;
    uint8_t reg_offset;

    if (irq >= 4 && irq <= 6) {
      /* MemManage (4), BusFault (5), UsageFault (6) */
      shpr_reg = SCB_SHPR1;
      reg_offset = (irq - 4) * 8;
    } else if (irq == 11) {
      /* SVCall (11) only */
      shpr_reg = SCB_SHPR2;
      reg_offset = 24; /* SVCall is at bits [31:24] of SHPR2 */
    } else if (irq == 12 || (irq >= 14 && irq <= 15)) {
      /* DebugMon (12), PendSV (14), SysTick (15) */
      shpr_reg = SCB_SHPR3;
      if (irq == 12) {
        reg_offset = 0; /* DebugMon is at bits [7:0] of SHPR3 */
      } else if (irq == 14) {
        reg_offset = 16; /* PendSV is at bits [23:16] of SHPR3 */
      } else {           /* irq == 15 */
        reg_offset = 24; /* SysTick is at bits [31:24] of SHPR3 */
      }
    } else {
      return POK_ERRNO_EINVAL;
    }

    /* Validate offset is within 32-bit register and properly aligned for 8-bit
     * priority field */
    if (reg_offset > 24 || (reg_offset & 7) != 0) {
#ifdef POK_NEEDS_DEBUG
      /* printf("ERROR: Invalid priority register offset %d for IRQ %d\n",
             reg_offset, irq); */
      pok_cons_write("ERROR: Invalid priority register offset\n", 38);
#endif
      return POK_ERRNO_EINVAL;
    }

    uint32_t mask = ~(ARM_PRIORITY_MASK << reg_offset);
    *shpr_reg = (*shpr_reg & mask) |
                ((priority << (8 - ARM_PRIORITY_BITS)) << reg_offset);
  } else {
    /* External interrupt priority */
    uint8_t external_irq = irq - EXCEPTION_IRQ0;
    NVIC_IPR[external_irq] = priority << (8 - ARM_PRIORITY_BITS);
  }

  /* Data Synchronization Barrier to ensure priority register write completes */
  __asm volatile("dsb" : : : "memory");

  return POK_ERRNO_OK;
}

pok_ret_t pok_nvic_clear_pending(uint8_t irq) {
  if (irq < EXCEPTION_IRQ0 || irq >= NVIC_VECTOR_COUNT) {
    return POK_ERRNO_EINVAL;
  }

  uint8_t external_irq = irq - EXCEPTION_IRQ0;
  uint32_t reg_idx = external_irq / 32;
  uint32_t bit_pos = external_irq % 32;

  NVIC_ICPR[reg_idx] = (1U << bit_pos);

  return POK_ERRNO_OK;
}

pok_ret_t pok_nvic_set_vector_table(uint32_t offset) {
  /* Vector table must be aligned to next power of 2 of table size */
  if (offset & (NVIC_VECTOR_TABLE_ALIGNMENT - 1)) {
    return POK_ERRNO_EINVAL;
  }

  /* Validate target address is in valid memory region (Flash or SRAM) */
  pok_bool_t in_flash = (offset >= STM32F4_FLASH_BASE &&
                         offset < (STM32F4_FLASH_BASE + STM32F4_FLASH_SIZE));
  pok_bool_t in_sram =
      (offset >= POK_SRAM_BASE && offset < (POK_SRAM_BASE + POK_SRAM_SIZE));

  if (!in_flash && !in_sram) {
#ifdef POK_NEEDS_DEBUG
    /* printf("ERROR: VTOR target at invalid address: 0x%x (outside Flash "
           "[0x%x-0x%x] or SRAM [0x%x-0x%x])\n",
           (unsigned int)offset, (unsigned int)STM32F4_FLASH_BASE,
           (unsigned int)(STM32F4_FLASH_BASE + STM32F4_FLASH_SIZE - 1),
           (unsigned int)POK_SRAM_BASE,
           (unsigned int)(POK_SRAM_BASE + POK_SRAM_SIZE - 1)); */
    pok_cons_write("ERROR: VTOR target at invalid address\n", 37);
#endif
    return POK_ERRNO_EFAULT;
  }

  /* Validate entire vector table fits within the target region */
  uint32_t region_end = in_flash ? (STM32F4_FLASH_BASE + STM32F4_FLASH_SIZE)
                                 : (POK_SRAM_BASE + POK_SRAM_SIZE);
  uint32_t table_end = offset + NVIC_VECTOR_TABLE_SIZE;

  if (table_end < offset || table_end > region_end) {
#ifdef POK_NEEDS_DEBUG
    /* printf("ERROR: Vector table at 0x%x (size %u bytes) extends beyond %s "
           "region end (0x%x)\n",
           (unsigned int)offset, NVIC_VECTOR_TABLE_SIZE,
           in_flash ? "Flash" : "SRAM", (unsigned int)region_end); */
    pok_cons_write("ERROR: Vector table extends beyond region end\n", 43);
#endif
    return POK_ERRNO_EFAULT;
  }

  *SCB_VTOR = offset;
  __asm volatile("dsb" : : : "memory");
  __asm volatile("isb");
  /* Keep relocation state consistent with VTOR */
  vector_table_relocated = (offset == (uint32_t)ram_vector_table) ? 1 : 0;
  return POK_ERRNO_OK;
}

/* Default exception handlers */
void NMI_Handler(void) { pok_nvic_default_handler(); }

void DebugMon_Handler(void) { pok_nvic_default_handler(); }
