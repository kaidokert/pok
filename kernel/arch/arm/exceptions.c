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
#include <core/sched.h>
/* Architecture-specific headers */
#include "arch.h"
#include "mpu.h"
#include "nvic.h"
#include "stm32f4/peripherals.h"

/* External declarations */
extern uint8_t pok_current_partition;

/* STM32F4 USART1 registers for non-blocking fault output */
#define USART1_SR (*((volatile uint32_t *)(USART1_BASE + 0x00)))
#define USART1_DR (*((volatile uint32_t *)(USART1_BASE + 0x04)))
#define USART_SR_TXE (1 << 7) /* Transmit data register empty */
#define USART_SR_PE (1 << 0)  /* Parity error */
#define USART_SR_FE (1 << 1)  /* Framing error */
#define USART_SR_ORE (1 << 3) /* Overrun error */

/* Non-blocking fault output functions with basic error handling */
static inline void fault_putc(char c) {
  volatile uint32_t status = USART1_SR;

  /* Check for UART errors - clear them but continue trying to output */
  if (status & (USART_SR_PE | USART_SR_FE | USART_SR_ORE)) {
    /* Clear error flags by reading SR then DR (hardware requirement) */
    (void)USART1_DR;
  }

  /* Try to output character without blocking if transmitter is ready */
  if (status & USART_SR_TXE) {
    USART1_DR = c;
  }
  /* If UART not ready, we silently drop the character - fault context
   * requires non-blocking operation for system stability */
}

static void fault_puts(const char *s) {
  while (*s) {
    fault_putc(*s++);
    /* Best-effort only; no delay to avoid prolonging fault handling */
  }
}

/* Static lookup table for hex conversion - avoids array allocation in fault
 * context */
static const char fault_hex_chars[16] = "0123456789ABCDEF";

static void fault_put_hex(uint32_t value) {
  fault_putc('0');
  fault_putc('x');

  /* Unrolled loop for better performance in fault context */
  fault_putc(fault_hex_chars[(value >> 28) & 0xF]);
  fault_putc(fault_hex_chars[(value >> 24) & 0xF]);
  fault_putc(fault_hex_chars[(value >> 20) & 0xF]);
  fault_putc(fault_hex_chars[(value >> 16) & 0xF]);
  fault_putc(fault_hex_chars[(value >> 12) & 0xF]);
  fault_putc(fault_hex_chars[(value >> 8) & 0xF]);
  fault_putc(fault_hex_chars[(value >> 4) & 0xF]);
  fault_putc(fault_hex_chars[value & 0xF]);
}

static void fault_put_dec(uint32_t value) {
  char buf[11]; /* Max 10 digits + null terminator for 32-bit value */
  int i = 0;

  if (value == 0) {
    fault_putc('0');
    return;
  }

  /* Build digits in reverse order - optimized division */
  while (value != 0) {
    buf[i++] = '0' + (value % 10);
    value /= 10;
  }

  /* Output digits in correct order */
  while (i > 0) {
    fault_putc(buf[--i]);
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
 * Macro to generate naked fault handler wrappers
 * Determines correct stack pointer (MSP vs PSP) based on EXC_RETURN
 */
#define DEFINE_FAULT_HANDLER_WRAPPER(handler_name, c_handler_name)             \
  void __attribute__((naked)) handler_name(void) {                             \
    __asm volatile(                                                            \
        "tst lr, #4                 \n" /* Test EXC_RETURN[2] */               \
        "ite eq                     \n" /* If-Then-Else */                     \
        "mrseq r0, msp              \n" /* If EXC_RETURN[2]==0, use MSP */     \
        "mrsne r0, psp              \n" /* If EXC_RETURN[2]==1, use PSP */     \
        "b " #c_handler_name                                                   \
        "      \n" /* Call C handler with correct frame */                     \
        ::                                                                     \
            : "r0", "memory");                                                 \
  }

DEFINE_FAULT_HANDLER_WRAPPER(MemManage_Handler, MemManage_Handler_C)

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
    __disable_irq(); /* Prevent livelock or nested faults */
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

  /* Clear MemManage fault flags in CFSR - write back only set bits */
  SCB_CFSR = cfsr & ARM_CFSR_MMFSR_MASK; /* Clear only set MMFSR bits */

  /* Get current partition */
  partition_id = pok_current_partition;

#ifdef POK_NEEDS_DEBUG
  fault_puts("=== MemManage Fault Analysis ===\n");
  fault_puts("Partition: ");
  fault_put_dec(partition_id);
  fault_puts("\nFault Address: ");
  if (cfsr & CFSR_MMARVALID) {
    fault_put_hex(fault_addr);
    fault_puts(" (valid)\n");
  } else {
    fault_puts("(not available)\n");
  }

  /* Detailed CFSR analysis */
  fault_puts("CFSR Flags: ");
  if (cfsr & (1 << 0))
    fault_puts("IACCVIOL ");
  if (cfsr & (1 << 1))
    fault_puts("DACCVIOL ");
  if (cfsr & (1 << 3))
    fault_puts("MUNSTKERR ");
  if (cfsr & (1 << 4))
    fault_puts("MSTKERR ");
  if (cfsr & (1 << 5))
    fault_puts("MLSPERR ");
  fault_puts("\n");

  /* Exception frame register dump */
  fault_puts("Exception Frame:\n");
  fault_puts("  r0: ");
  fault_put_hex(frame[0]);
  fault_puts("\n");
  fault_puts("  r1: ");
  fault_put_hex(frame[1]);
  fault_puts("\n");
  fault_puts("  r2: ");
  fault_put_hex(frame[2]);
  fault_puts("\n");
  fault_puts("  r3: ");
  fault_put_hex(frame[3]);
  fault_puts("\n");
  fault_puts("  r12:");
  fault_put_hex(frame[4]);
  fault_puts("\n");
  fault_puts("  LR: ");
  fault_put_hex(frame[5]);
  fault_puts("\n");
  fault_puts("  PC: ");
  fault_put_hex(frame[6]);
  fault_puts("\n");
  fault_puts("  PSR:");
  fault_put_hex(frame[7]);
  fault_puts("\n");

  /* Stack pointer analysis */
  fault_puts("Stack: frame@");
  fault_put_hex((uint32_t)frame);
  fault_puts(", size=32 bytes\n");
#endif

  /* For safety-critical systems, halt immediately on memory faults
   * instead of attempting complex recovery from fault context */
#ifdef POK_NEEDS_DEBUG
  fault_puts("FATAL: Memory protection violation in partition ");
  fault_put_dec(partition_id);
  fault_puts(" - System halted for safety\n");
#endif

  /* Halt the system - safer than attempting partition recovery from fault
   * handler */
  __disable_irq(); /* Prevent livelock or nested faults */
  while (1) {
    __asm volatile("wfi");
  }
}

DEFINE_FAULT_HANDLER_WRAPPER(BusFault_Handler, BusFault_Handler_C)

/*
 * Bus Fault Handler - C implementation
 * Handles bus errors and invalid memory accesses
 */
static void BusFault_Handler_C(uint32_t *frame) {
  uint32_t fault_addr = 0;
  uint8_t partition_id;
  uint32_t cfsr;

  if (frame == NULL) {
    __disable_irq();
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

  /* Clear Bus fault flags in CFSR - write back only set bits */
  SCB_CFSR = cfsr & ARM_CFSR_BFSR_MASK; /* Clear only set BFSR bits */

  partition_id = pok_current_partition;

#ifdef POK_NEEDS_DEBUG
  fault_puts("=== BusFault Analysis ===\n");
  fault_puts("Partition: ");
  fault_put_dec(partition_id);
  fault_puts("\nFault Address: ");
  if (cfsr & CFSR_BFARVALID) {
    fault_put_hex(fault_addr);
    fault_puts(" (valid)\n");
  } else {
    fault_puts("(not available)\n");
  }

  /* Detailed BFSR analysis */
  fault_puts("BFSR Flags: ");
  if (cfsr & (1 << 8))
    fault_puts("IBUSERR ");
  if (cfsr & (1 << 9))
    fault_puts("PRECISERR ");
  if (cfsr & (1 << 10))
    fault_puts("IMPRECISERR ");
  if (cfsr & (1 << 11))
    fault_puts("UNSTKERR ");
  if (cfsr & (1 << 12))
    fault_puts("STKERR ");
  if (cfsr & (1 << 13))
    fault_puts("LSPERR ");
  fault_puts("\n");

  /* Exception frame dump */
  fault_puts("Exception Frame: PC=");
  fault_put_hex(frame[6]);
  fault_puts(", LR=");
  fault_put_hex(frame[5]);
  fault_puts(", PSR=");
  fault_put_hex(frame[7]);
  fault_puts("\n");
#endif

  /* Halt system immediately - safer than partition recovery from fault context
   */
#ifdef POK_NEEDS_DEBUG
  fault_puts("FATAL: Bus fault recovery disabled - System halted for safety\n");
#endif
  __disable_irq(); /* Prevent livelock or nested faults */
  while (1) {
    __asm volatile("wfi");
  }
}

DEFINE_FAULT_HANDLER_WRAPPER(UsageFault_Handler, UsageFault_Handler_C)

/*
 * Usage Fault Handler - C implementation
 * Handles undefined instruction, unaligned access, etc.
 */
static void UsageFault_Handler_C(uint32_t *frame) {
  uint8_t partition_id;
  uint32_t cfsr;

  if (frame == NULL) {
    __disable_irq();
    while (1) {
      __asm volatile("wfi");
    }
  }

  partition_id = pok_current_partition;

  /* Read CFSR to check fault status */
  cfsr = SCB_CFSR;

  /* Clear Usage fault flags in CFSR - write back only set bits */
  SCB_CFSR = cfsr & ARM_CFSR_UFSR_MASK; /* Clear only set UFSR bits */

#ifdef POK_NEEDS_DEBUG
  fault_puts("=== UsageFault Analysis ===\n");
  fault_puts("Partition: ");
  fault_put_dec(partition_id);
  fault_puts("\n");

  /* Detailed UFSR analysis */
  fault_puts("UFSR Flags: ");
  if (cfsr & (1 << 16))
    fault_puts("UNDEFINSTR ");
  if (cfsr & (1 << 17))
    fault_puts("INVSTATE ");
  if (cfsr & (1 << 18))
    fault_puts("INVPC ");
  if (cfsr & (1 << 19))
    fault_puts("NOCP ");
  if (cfsr & (1 << 24))
    fault_puts("UNALIGNED ");
  if (cfsr & (1 << 25))
    fault_puts("DIVBYZERO ");
  fault_puts("\n");

  /* Exception context */
  fault_puts("Context: PC=");
  fault_put_hex(frame[6]);
  fault_puts(", LR=");
  fault_put_hex(frame[5]);
  fault_puts(", PSR=");
  fault_put_hex(frame[7]);
  fault_puts("\nInstruction at fault: ");

  /* Try to read instruction at PC (be careful with memory access) */
  uint32_t pc = frame[6] & ~1;               /* Clear Thumb bit */
  if (pc >= 0x08000000 && pc < 0x08100000) { /* Within Flash range */
    uint16_t instruction = *((volatile uint16_t *)pc);
    fault_put_hex(instruction);
  } else {
    fault_puts("(invalid PC)");
  }
  fault_puts("\n");
#endif

  /* Halt system immediately - safer than partition recovery from fault context
   */
#ifdef POK_NEEDS_DEBUG
  fault_puts("FATAL: Usage fault in partition ");
  fault_put_dec(partition_id);
  fault_puts(" - System halted for safety\n");
#endif
  __disable_irq(); /* Prevent livelock or nested faults */
  while (1) {
    __asm volatile("wfi");
  }
}

/*
 * Hard Fault Handler
 * Last resort fault handler
 */
DEFINE_FAULT_HANDLER_WRAPPER(HardFault_Handler, HardFault_Handler_C)

/*
 * Hard Fault Handler - C implementation
 */
static void HardFault_Handler_C(uint32_t *frame) {
  uint8_t partition_id;

  if (frame == NULL) {
    __disable_irq();
    while (1) {
      __asm volatile("wfi");
    }
  }

  partition_id = pok_current_partition;

#ifdef POK_NEEDS_DEBUG
  fault_puts("HardFault in partition ");
  fault_put_dec(partition_id);
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

  /* Halt system immediately - HardFault indicates severe system error */
#ifdef POK_NEEDS_DEBUG
  fault_puts(
      "FATAL: Hard fault recovery disabled - System halted for safety\n");
#endif
  __disable_irq(); /* Prevent livelock or nested faults */
  while (1) {
    __asm volatile("wfi");
  }
}
