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
 * \file    arch/arm/exceptions.c
 * \author  POK team
 * \brief   ARM Cortex-M exception handling
 */

/* POK system headers */
#include <errno.h>

/* POK core headers */
#include <core/debug.h>
#include <core/partition.h>

/* Architecture-specific headers */
#include "arch.h"
#include "mpu.h"
#include "nvic.h"

/* STM32F4 USART1 registers for non-blocking fault output */
#define USART1_SR         (*((volatile uint32_t *)(0x40011000 + 0x00)))
#define USART1_DR         (*((volatile uint32_t *)(0x40011000 + 0x04)))
#define USART_SR_TXE      (1 << 7)  /* Transmit data register empty */

/* Non-blocking fault output functions */
static inline void fault_putc(char c) {
  /* Try to output character without blocking */
  if (USART1_SR & USART_SR_TXE) {
    USART1_DR = c;
  }
}

static void fault_puts(const char *s) {
  while (*s) {
    fault_putc(*s++);
    /* Small delay to allow UART to catch up */
    for (volatile int i = 0; i < 1000; i++);
  }
}

static void fault_put_hex(uint32_t value) {
  const char hex_chars[] = "0123456789ABCDEF";
  fault_puts("0x");
  for (int i = 28; i >= 0; i -= 4) {
    fault_putc(hex_chars[(value >> i) & 0xF]);
  }
}

/* CFSR (Configurable Fault Status Register) bits */
#define SCB_CFSR (*((volatile uint32_t *)(SCB_BASE + 0x28)))
#define CFSR_MMARVALID (1 << 7)  /* MemManage Fault Address Register valid */
#define CFSR_BFARVALID (1 << 15) /* Bus Fault Address Register valid */

/* Forward declarations for the actual handlers */
static void MemManage_Handler_C(uint32_t *frame);
static void BusFault_Handler_C(uint32_t *frame);
static void UsageFault_Handler_C(uint32_t *frame);
static void HardFault_Handler_C(uint32_t *frame);

/*
 * Memory Management Fault Handler - Naked wrapper
 * Determines correct stack pointer (MSP vs PSP) based on EXC_RETURN
 */
void __attribute__((naked)) MemManage_Handler(void) {
  __asm volatile(
      "tst lr, #4                 \n" /* Test EXC_RETURN[2] */
      "ite eq                     \n" /* If-Then-Else */
      "mrseq r0, msp              \n" /* If EXC_RETURN[2]==0, use MSP */
      "mrsne r0, psp              \n" /* If EXC_RETURN[2]==1, use PSP */
      "b MemManage_Handler_C      \n" /* Call C handler with correct frame */
      ::
          : "r0", "memory");
}

/*
 * Memory Management Fault Handler - C implementation
 * Handles MPU violations and other memory management faults
 */
static void MemManage_Handler_C(uint32_t *frame) {
  uint32_t fault_addr = 0;
  uint8_t partition_id;
  uint32_t cfsr;

  if (frame == NULL) {
    /* Cannot recover from null frame, halt system */
    while (1) {
      __asm volatile("wfi");
    }
  }

  /* Read CFSR to check fault status */
  cfsr = SCB_CFSR;

  /* Get faulting address from MemManage Fault Address Register if valid */
  if (cfsr & CFSR_MMARVALID) {
    fault_addr = *((volatile uint32_t *)(SCB_BASE + 0x34)); /* MMFAR */
  }

  /* Clear MemManage fault flags in CFSR */
  SCB_CFSR = cfsr & ARM_CFSR_MMFSR_MASK; /* Clear MMFSR bits */

  /* Get current partition */
  extern uint8_t pok_current_partition;
  partition_id = pok_current_partition;

#ifdef POK_NEEDS_DEBUG
  if (cfsr & CFSR_MMARVALID) {
    fault_puts("MemManage fault in partition ");
    fault_putc('0' + partition_id);
    fault_puts(" at address ");
    fault_put_hex(fault_addr);
    fault_puts("\n");
  } else {
    fault_puts("MemManage fault in partition ");
    fault_putc('0' + partition_id);
    fault_puts(" (address not available)\n");
  }
  fault_puts("PC: ");
  fault_put_hex(frame[6]);
  fault_puts(", LR: ");
  fault_put_hex(frame[5]);
  fault_puts(", CFSR: ");
  fault_put_hex(cfsr);
  fault_puts("\n");
#endif

  /* Handle partition isolation violation */
  if (partition_id < POK_CONFIG_NB_PARTITIONS) {
    /* Terminate the offending partition */
    pok_partition_set_mode(partition_id, POK_PARTITION_MODE_STOPPED);

    /* Force a reschedule to switch away from this partition */
    pok_sched_end_period();
  }

  /* If we reach here, halt the system */
  while (1) {
    __asm volatile("wfi");
  }
}

/*
 * Bus Fault Handler - Naked wrapper
 * Determines correct stack pointer (MSP vs PSP) based on EXC_RETURN
 */
void __attribute__((naked)) BusFault_Handler(void) {
  __asm volatile(
      "tst lr, #4                 \n" /* Test EXC_RETURN[2] */
      "ite eq                     \n" /* If-Then-Else */
      "mrseq r0, msp              \n" /* If EXC_RETURN[2]==0, use MSP */
      "mrsne r0, psp              \n" /* If EXC_RETURN[2]==1, use PSP */
      "b BusFault_Handler_C       \n" /* Call C handler with correct frame */
      ::
          : "r0", "memory");
}

/*
 * Bus Fault Handler - C implementation
 * Handles bus errors and invalid memory accesses
 */
static void BusFault_Handler_C(uint32_t *frame) {
  uint32_t fault_addr = 0;
  uint8_t partition_id;
  uint32_t cfsr;

  if (frame == NULL) {
    while (1) {
      __asm volatile("wfi");
    }
  }

  /* Read CFSR to check fault status */
  cfsr = SCB_CFSR;

  /* Get faulting address from Bus Fault Address Register if valid */
  if (cfsr & CFSR_BFARVALID) {
    fault_addr = *((volatile uint32_t *)(SCB_BASE + 0x38)); /* BFAR */
  }

  /* Clear Bus fault flags in CFSR */
  SCB_CFSR = (cfsr & ARM_CFSR_BFSR_MASK); /* Clear BFSR bits (no shift needed) */

  extern uint8_t pok_current_partition;
  partition_id = pok_current_partition;

#ifdef POK_NEEDS_DEBUG
  if (cfsr & CFSR_BFARVALID) {
    fault_puts("BusFault in partition ");
    fault_putc('0' + partition_id);
    fault_puts(" at address ");
    fault_put_hex(fault_addr);
    fault_puts("\n");
  } else {
    fault_puts("BusFault in partition ");
    fault_putc('0' + partition_id);
    fault_puts(" (address not available)\n");
  }
  fault_puts("PC: ");
  fault_put_hex(frame[6]);
  fault_puts(", LR: ");
  fault_put_hex(frame[5]);
  fault_puts(", CFSR: ");
  fault_put_hex(cfsr);
  fault_puts("\n");
#endif

  if (partition_id < POK_CONFIG_NB_PARTITIONS) {
    pok_partition_set_mode(partition_id, POK_PARTITION_MODE_STOPPED);
    pok_sched_end_period();
  }

  while (1) {
    __asm volatile("wfi");
  }
}

/*
 * Usage Fault Handler - Naked wrapper
 * Determines correct stack pointer (MSP vs PSP) based on EXC_RETURN
 */
void __attribute__((naked)) UsageFault_Handler(void) {
  __asm volatile(
      "tst lr, #4                 \n" /* Test EXC_RETURN[2] */
      "ite eq                     \n" /* If-Then-Else */
      "mrseq r0, msp              \n" /* If EXC_RETURN[2]==0, use MSP */
      "mrsne r0, psp              \n" /* If EXC_RETURN[2]==1, use PSP */
      "b UsageFault_Handler_C     \n" /* Call C handler with correct frame */
      ::
          : "r0", "memory");
}

/*
 * Usage Fault Handler - C implementation
 * Handles undefined instruction, unaligned access, etc.
 */
static void UsageFault_Handler_C(uint32_t *frame) {
  uint8_t partition_id;

  if (frame == NULL) {
    while (1) {
      __asm volatile("wfi");
    }
  }

  extern uint8_t pok_current_partition;
  partition_id = pok_current_partition;

  /* Clear Usage fault flags in CFSR */
  uint32_t cfsr = SCB_CFSR;
  SCB_CFSR = (cfsr & ARM_CFSR_UFSR_MASK); /* Clear UFSR bits (upper 16 bits) */

#ifdef POK_NEEDS_DEBUG
  fault_puts("UsageFault in partition ");
  fault_putc('0' + partition_id);
  fault_puts("\n");
  fault_puts("PC: ");
  fault_put_hex(frame[6]);
  fault_puts(", LR: ");
  fault_put_hex(frame[5]);
  fault_puts("\n");
#endif

  if (partition_id < POK_CONFIG_NB_PARTITIONS) {
    pok_partition_set_mode(partition_id, POK_PARTITION_MODE_STOPPED);
    pok_sched_end_period();
  }

  while (1) {
    __asm volatile("wfi");
  }
}

/*
 * Hard Fault Handler
 * Last resort fault handler
 */
/*
 * Hard Fault Handler - Naked wrapper
 * Determines correct stack pointer (MSP vs PSP) based on EXC_RETURN
 */
void __attribute__((naked)) HardFault_Handler(void) {
  __asm volatile(
      "tst lr, #4                 \n" /* Test EXC_RETURN[2] */
      "ite eq                     \n" /* If-Then-Else */
      "mrseq r0, msp              \n" /* If EXC_RETURN[2]==0, use MSP */
      "mrsne r0, psp              \n" /* If EXC_RETURN[2]==1, use PSP */
      "b HardFault_Handler_C      \n" /* Call C handler with correct frame */
      ::
          : "r0", "memory");
}

/*
 * Hard Fault Handler - C implementation
 */
static void HardFault_Handler_C(uint32_t *frame) {
  uint8_t partition_id;

  if (frame == NULL) {
    while (1) {
      __asm volatile("wfi");
    }
  }

  extern uint8_t pok_current_partition;
  partition_id = pok_current_partition;

#ifdef POK_NEEDS_DEBUG
  fault_puts("HardFault in partition ");
  fault_putc('0' + partition_id);
  fault_puts("\n");
  fault_puts("PC: ");
  fault_put_hex(frame[6]);
  fault_puts(", LR: ");
  fault_put_hex(frame[5]);
  fault_puts(", PSR: ");
  fault_put_hex(frame[7]);
  fault_puts("\n");
  fault_puts("r0: ");
  fault_put_hex(frame[0]);
  fault_puts(", r1: ");
  fault_put_hex(frame[1]);
  fault_puts(", r2: ");
  fault_put_hex(frame[2]);
  fault_puts(", r3: ");
  fault_put_hex(frame[3]);
  fault_puts("\n");
#endif

  /* Try to recover by stopping the current partition */
  if (partition_id < POK_CONFIG_NB_PARTITIONS) {
    pok_partition_set_mode(partition_id, POK_PARTITION_MODE_STOPPED);
    pok_sched_end_period();
  }

  /* If recovery fails, halt the system */
  while (1) {
    __asm volatile("wfi");
  }
}
