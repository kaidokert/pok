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

#include "nvic.h"
#include <errno.h>
#include <libc.h>

/* External vector table (defined in startup code) */
extern vector_table_entry_t vector_table[];

/* Default handlers */
static void pok_nvic_default_handler(void) {
  /* Default handler - infinite loop */
  while (1) {
    __asm volatile("wfi");
  }
}

pok_ret_t pok_nvic_init(void) {
  /* Enable memory management, bus fault, and usage fault exceptions */
  SCB_SHCSR |=
      SCB_SHCSR_MEMFAULTENA | SCB_SHCSR_BUSFAULTENA | SCB_SHCSR_USGFAULTENA;

  /* Set PendSV and SysTick to lowest priority for context switching */
  pok_nvic_set_priority(EXCEPTION_PENDSV, NVIC_PRIORITY_LOWEST);
  pok_nvic_set_priority(EXCEPTION_SYSTICK, NVIC_PRIORITY_LOWEST);

  /* Clear all pending interrupts */
  for (int i = 0; i < 8; i++) {
    NVIC_ICPR[i] = 0xFFFFFFFF;
  }

  return (POK_ERRNO_OK);
}

pok_ret_t pok_nvic_set_handler(uint8_t irq, void (*handler)(void)) {
  if (irq >= NVIC_MAX_IRQ) {
    return (POK_ERRNO_EINVAL);
  }

  /* Set handler in vector table */
  if (handler == NULL) {
    vector_table[irq] = pok_nvic_default_handler;
  } else {
    vector_table[irq] = handler;
  }

  return (POK_ERRNO_OK);
}

pok_ret_t pok_nvic_enable_irq(uint8_t irq) {
  if (irq < EXCEPTION_IRQ0 || irq >= NVIC_MAX_IRQ) {
    return (POK_ERRNO_EINVAL);
  }

  uint8_t external_irq = irq - EXCEPTION_IRQ0;
  uint32_t reg_idx = external_irq / 32;
  uint32_t bit_pos = external_irq % 32;

  NVIC_ISER[reg_idx] = (1 << bit_pos);

  return (POK_ERRNO_OK);
}

pok_ret_t pok_nvic_disable_irq(uint8_t irq) {
  if (irq < EXCEPTION_IRQ0 || irq >= NVIC_MAX_IRQ) {
    return (POK_ERRNO_EINVAL);
  }

  uint8_t external_irq = irq - EXCEPTION_IRQ0;
  uint32_t reg_idx = external_irq / 32;
  uint32_t bit_pos = external_irq % 32;

  NVIC_ICER[reg_idx] = (1 << bit_pos);

  return (POK_ERRNO_OK);
}

pok_ret_t pok_nvic_set_priority(uint8_t irq, uint8_t priority) {
  if (irq >= NVIC_MAX_IRQ || priority > NVIC_PRIORITY_LOWEST) {
    return (POK_ERRNO_EINVAL);
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
      reg_offset = 0; /* SVCall is at bits [7:0] of SHPR2 */
    } else if (irq == 12 || (irq >= 14 && irq <= 15)) {
      /* DebugMon (12), PendSV (14), SysTick (15) */
      shpr_reg = (uint32_t *)&SCB_SHPR3;
      if (irq == 12) {
        reg_offset = 0; /* DebugMon is at bits [7:0] of SHPR3 */
      } else {
        reg_offset =
            (irq - 14) * 8 + 8; /* PendSV at [15:8], SysTick at [23:16] */
      }
    } else {
      return (POK_ERRNO_EINVAL);
    }

    /* Ensure offset doesn't exceed register bounds (24 bits max) */
    if (reg_offset > 24) {
      return (POK_ERRNO_EINVAL);
    }

    uint32_t mask = ~(0xFFu << reg_offset);
    *shpr_reg = (*shpr_reg & mask) | ((priority << 4) << reg_offset);
  } else {
    /* External interrupt priority */
    uint8_t external_irq = irq - EXCEPTION_IRQ0;
    NVIC_IPR[external_irq] = priority << 4;
  }

  return (POK_ERRNO_OK);
}

pok_ret_t pok_nvic_clear_pending(uint8_t irq) {
  if (irq < EXCEPTION_IRQ0 || irq >= NVIC_MAX_IRQ) {
    return (POK_ERRNO_EINVAL);
  }

  uint8_t external_irq = irq - EXCEPTION_IRQ0;
  uint32_t reg_idx = external_irq / 32;
  uint32_t bit_pos = external_irq % 32;

  NVIC_ICPR[reg_idx] = (1 << bit_pos);

  return (POK_ERRNO_OK);
}

pok_ret_t pok_nvic_set_vector_table(uint32_t offset) {
  /* Vector table must be aligned to 128 bytes minimum */
  if (offset & 0x7F) {
    return (POK_ERRNO_EINVAL);
  }

  SCB_VTOR = offset;

  return (POK_ERRNO_OK);
}

/* Default exception handlers */
void NMI_Handler(void) { pok_nvic_default_handler(); }

void DebugMon_Handler(void) { pok_nvic_default_handler(); }