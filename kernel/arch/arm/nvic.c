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
#include <libc.h>

/* Architecture-specific headers */
#include "arch.h"
#include "cortex_m_config.h"
#include "nvic.h"

/* External vector table (defined in startup code) */
extern vector_table_entry_t vector_table[]; /* Original ROM vector table */

/* RAM-based vector table for runtime handler updates */
#define NVIC_VECTOR_COUNT CORTEX_M_NVIC_VECTOR_COUNT
#define NVIC_VECTOR_TABLE_SIZE CORTEX_M_NVIC_VECTOR_TABLE_SIZE
#define NVIC_VECTOR_TABLE_ALIGNMENT CORTEX_M_NVIC_VECTOR_TABLE_ALIGNMENT
static vector_table_entry_t ram_vector_table[NVIC_VECTOR_COUNT]
    __attribute__((aligned(NVIC_VECTOR_TABLE_ALIGNMENT)));
static uint8_t vector_table_relocated = 0;

/* Default handlers */
static void pok_nvic_default_handler(void) {
  /* Default handler - infinite loop */
  while (1) {
    __asm volatile("wfi");
  }
}

/**
 * Relocate vector table from FLASH to RAM for runtime handler updates
 */
static pok_ret_t pok_nvic_relocate_vector_table(void) {
  if (vector_table_relocated) {
    return POK_ERRNO_OK; /* Already relocated */
  }

  /* Copy ROM vector table to RAM */
  for (int i = 0; i < NVIC_VECTOR_COUNT; i++) {
    ram_vector_table[i] = vector_table[i];
  }

  /* Update VTOR register to point to RAM vector table */
  uint32_t ram_table_addr = (uint32_t)ram_vector_table;

  /* Validate alignment (must be next power of 2 of table size) */
  if (ram_table_addr & (NVIC_VECTOR_TABLE_ALIGNMENT - 1)) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: RAM vector table not properly aligned: 0x%x (required: %d "
           "bytes)\n",
           ram_table_addr, NVIC_VECTOR_TABLE_ALIGNMENT);
#endif
    return POK_ERRNO_EFAULT;
  }

  SCB_VTOR = ram_table_addr;
  vector_table_relocated = 1;

#ifdef POK_NEEDS_DEBUG
  printf("Vector table relocated to RAM at 0x%x\n", ram_table_addr);
#endif

  __asm volatile("dsb" ::: "memory");
  __asm volatile("isb");
  return POK_ERRNO_OK;
  return POK_ERRNO_OK;
}

pok_ret_t pok_nvic_init(void) {
  pok_ret_t ret;

  /* Relocate vector table to RAM for runtime handler updates */
  ret = pok_nvic_relocate_vector_table();
  if (ret != POK_ERRNO_OK) {
    return (ret);
  }

  /* Enable division-by-zero trap to trigger UsageFault */
  SCB_CCR |= SCB_CCR_DIV_0_TRP;

  /* Enable memory management, bus fault, and usage fault exceptions */
  SCB_SHCSR |=
      SCB_SHCSR_MEMFAULTENA | SCB_SHCSR_BUSFAULTENA | SCB_SHCSR_USGFAULTENA;

  /* Set fault handlers to high priority for proper error handling */
  pok_nvic_set_priority(EXCEPTION_MEMMANAGE, NVIC_PRIORITY_HIGH);
  pok_nvic_set_priority(EXCEPTION_BUSFAULT, NVIC_PRIORITY_HIGH);
  pok_nvic_set_priority(EXCEPTION_USAGEFAULT, NVIC_PRIORITY_HIGH);

  /* Set PendSV and SysTick to lowest priority for context switching */
  pok_nvic_set_priority(EXCEPTION_PENDSV, NVIC_PRIORITY_LOWEST);
  pok_nvic_set_priority(EXCEPTION_SYSTICK, NVIC_PRIORITY_LOWEST);

  /* Clear all pending interrupts */
  for (int i = 0; i < 8; i++) {
    NVIC_ICPR[i] = 0xFFFFFFFF;
  }

  return POK_ERRNO_OK;
}

pok_ret_t pok_nvic_set_handler(uint8_t irq, void (*handler)(void)) {
  if (irq >= NVIC_VECTOR_COUNT) {
    return POK_ERRNO_EINVAL;
  }

  /* Ensure vector table has been relocated to RAM */
  if (!vector_table_relocated) {
    pok_ret_t ret = pok_nvic_relocate_vector_table();
    if (ret != POK_ERRNO_OK) {
      return (ret);
    }
  }

  /* Disable IRQ during handler update to prevent race conditions */
  uint8_t irq_was_enabled = 0;
  if (irq >= EXCEPTION_IRQ0) {
    /* Check if external IRQ was enabled */
    uint8_t external_irq = irq - EXCEPTION_IRQ0;
    uint32_t reg_idx = external_irq / 32;
    uint32_t bit_pos = external_irq % 32;
    if (NVIC_ISER[reg_idx] & (1 << bit_pos)) {
      irq_was_enabled = 1;
      NVIC_ICER[reg_idx] = (1 << bit_pos); /* Disable IRQ */
    }
  }

  /* Set handler in RAM vector table */
  if (handler == NULL) {
    ram_vector_table[irq] = pok_nvic_default_handler;
  } else {
    ram_vector_table[irq] = handler;
  }

  /* Data Synchronization Barrier to ensure vector table update completes */
  __asm volatile("dsb" : : : "memory");
  __asm volatile("isb");
  /* Re-enable IRQ if it was enabled before */
  if (irq_was_enabled) {
    uint8_t external_irq = irq - EXCEPTION_IRQ0;
    uint32_t reg_idx = external_irq / 32;
    uint32_t bit_pos = external_irq % 32;
    NVIC_ISER[reg_idx] = (1 << bit_pos);
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

  NVIC_ISER[reg_idx] = (1 << bit_pos);

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

  NVIC_ICER[reg_idx] = (1 << bit_pos);

  /* Data Synchronization Barrier to ensure register write completes */
  __asm volatile("dsb" : : : "memory");

  return POK_ERRNO_OK;
}

pok_ret_t pok_nvic_set_priority(uint8_t irq, uint8_t priority) {
  if (irq >= NVIC_VECTOR_COUNT || priority > NVIC_PRIORITY_LOWEST) {
    return POK_ERRNO_EINVAL;
  }

  if (irq < EXCEPTION_IRQ0) {
    /* System exception priority */
    uint32_t *shpr_reg;
    uint8_t reg_offset;

    if (irq >= 4 && irq <= 6) {
      /* MemManage (4), BusFault (5), UsageFault (6) */
      shpr_reg = (uint32_t *)&SCB_SHPR1;
      reg_offset = (irq - 4) * 8;
    } else if (irq == 11) {
      /* SVCall (11) only */
      shpr_reg = (uint32_t *)&SCB_SHPR2;
      reg_offset = 24; /* SVCall is at bits [31:24] of SHPR2 */
    } else if (irq == 12 || (irq >= 14 && irq <= 15)) {
      /* DebugMon (12), PendSV (14), SysTick (15) */
      shpr_reg = (uint32_t *)&SCB_SHPR3;
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

    /* Ensure offset doesn't exceed register bounds (24 bits max) */
    if (reg_offset > 24) {
      return POK_ERRNO_EINVAL;
    }

    uint32_t mask = ~(ARM_PRIORITY_MASK << reg_offset);
    *shpr_reg = (*shpr_reg & mask) | ((priority << 4) << reg_offset);
  } else {
    /* External interrupt priority */
    uint8_t external_irq = irq - EXCEPTION_IRQ0;
    NVIC_IPR[external_irq] = priority << 4;
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

  NVIC_ICPR[reg_idx] = (1 << bit_pos);

  return POK_ERRNO_OK;
}

pok_ret_t pok_nvic_set_vector_table(uint32_t offset) {
  /* Vector table must be aligned to next power of 2 of table size */
  if (offset & (NVIC_VECTOR_TABLE_ALIGNMENT - 1)) {
    return POK_ERRNO_EINVAL;
  }

  SCB_VTOR = offset;
  __asm volatile("dsb" : : : "memory");
  __asm volatile("isb");
  return POK_ERRNO_OK;
}

/* Default exception handlers */
void NMI_Handler(void) { pok_nvic_default_handler(); }

void DebugMon_Handler(void) { pok_nvic_default_handler(); }
