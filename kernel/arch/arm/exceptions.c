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
#include <bsp.h>
#include <errno.h>

/* POK core headers */
#include <core/debug.h>
#include <core/partition.h>
#include <core/sched.h>
/* Architecture-specific headers */
#include "arch.h"
#include "mpu.h"
#include "nvic.h"
/* BSP abstraction - no direct BSP includes in arch layer */

/* ARM Cortex-M intrinsics - define locally to avoid CMSIS dependency */
#define __disable_irq() __asm volatile("cpsid i" ::: "memory")

/* External declarations */
extern uint8_t pok_current_partition;

/* BSP abstraction for fault output - BSP layer provides implementation */
extern void (*pok_bsp_fault_putchar)(char c);

/* BSP abstraction for memory ranges - BSP layer provides these */
extern uint32_t pok_bsp_flash_base;
extern uint32_t pok_bsp_flash_size;

/* Architecture-layer fault output using BSP abstraction */
static inline void fault_putc(char c) {
  /* Use BSP-provided fault output function if available */
  if (pok_bsp_fault_putchar != NULL) {
    pok_bsp_fault_putchar(c);
  }
  /* If BSP doesn't provide fault output, silently drop character
   * Fault context requires non-blocking operation for system stability */
}

static void __attribute__((unused)) fault_puts(const char *s) {
  while (*s) {
    fault_putc(*s++);
    /* Best-effort only; no delay to avoid prolonging fault handling */
  }
}

/* Static lookup table for hex conversion - avoids array allocation in fault
 * context */
static const char fault_hex_chars[16] = "0123456789ABCDEF";

static void __attribute__((unused)) fault_put_hex(uint32_t value) {
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

static void __attribute__((unused)) fault_put_dec(uint32_t value) {
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
static void __attribute__((used)) MemManage_Handler_C(uint32_t *frame);
static void __attribute__((used)) BusFault_Handler_C(uint32_t *frame);
static void __attribute__((used)) UsageFault_Handler_C(uint32_t *frame);
static void __attribute__((used)) HardFault_Handler_C(uint32_t *frame);

/*
 * Macro to generate naked fault handler wrappers
 * Determines correct stack pointer (MSP vs PSP) based on EXC_RETURN
 */
#define DEFINE_FAULT_HANDLER_WRAPPER(handler_name, c_handler_name)             \
  void __attribute__((naked, noinline, no_instrument_function)) handler_name(  \
      void) {                                                                  \
    __asm volatile(                                                            \
        "tst lr, #4                 \n" /* Test EXC_RETURN[2] */               \
        "ite eq                     \n" /* If-Then-Else */                     \
        "mrseq r0, msp              \n" /* If EXC_RETURN[2]==0, use MSP */     \
        "mrsne r0, psp              \n" /* If EXC_RETURN[2]==1, use PSP */     \
        "b " #c_handler_name                                                   \
        "      \n" /* Call C handler with correct frame */                     \
        ::                                                                     \
            : "r0", "cc", "memory");                                           \
  }

DEFINE_FAULT_HANDLER_WRAPPER(MemManage_Handler, MemManage_Handler_C)

/*
 * Memory Management Fault Handler - C implementation
 * Handles MPU violations and other memory management faults
 */
static void __attribute__((used)) MemManage_Handler_C(uint32_t *frame) {
  uint32_t fault_addr __attribute__((unused)) = 0;
  uint8_t partition_id __attribute__((unused));
  uint32_t cfsr;

  pok_cons_write("!!! MEMMANAGE FAULT !!!\n", 24);

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

  /* Clear MemManage fault flags in CFSR using write-1-to-clear semantics
   * Only clear the MMFSR bits that are actually set to avoid affecting
   * BFSR/UFSR ARM_CFSR_MMFSR_MASK is defined in arch.h as 0xFFu */
  SCB_CFSR = cfsr & ARM_CFSR_MMFSR_MASK; /* Write 1s to clear set MMFSR bits */

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

  /* DESIGN DECISION: Hard-halt on memory faults for safety-critical systems
   *
   * This implementation prioritizes safety over availability by immediately
   * halting the system on memory protection violations. This design choice
   * is appropriate for safety-critical embedded systems because:
   *
   * 1. Memory faults often indicate serious bugs that could compromise safety
   * 2. Attempting fault recovery in interrupt context is complex and risky
   * 3. Hardware watchdog timers provide system-level recovery mechanism
   * 4. Fail-safe behavior prevents potentially dangerous continued execution
   *
   * Alternative approaches (partition recovery, isolation) would be more
   * suitable for general-purpose systems but add complexity and potential
   * attack vectors in safety-critical contexts.
   *
   * For applications requiring different fault handling, this can be
   * customized via conditional compilation or callback mechanisms.
   */
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
static void __attribute__((used)) BusFault_Handler_C(uint32_t *frame) {
  uint32_t fault_addr __attribute__((unused)) = 0;
  uint8_t partition_id __attribute__((unused));
  uint32_t cfsr;

  pok_cons_write("!!! BUSFAULT !!!\n", 17);

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

  /* Clear Bus fault flags in CFSR using write-1-to-clear semantics
   * Only clear the BFSR bits that are actually set to avoid affecting
   * MMFSR/UFSR */
  SCB_CFSR = cfsr & ARM_CFSR_BFSR_MASK; /* Write 1s to clear set BFSR bits */

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

  /* PendSV debug values */
  extern volatile uint32_t pendsv_debug_flag_value;
  extern volatile uint32_t pendsv_debug_r0_after_load;
  extern volatile uint32_t pendsv_debug_r2_addr;
  extern volatile uint32_t pendsv_debug_psp_after_set;
  fault_puts("PendSV Debug:\n");
  fault_puts("  flag_value=");
  fault_put_hex(pendsv_debug_flag_value);
  fault_puts("\n  r0_after_load=");
  fault_put_hex(pendsv_debug_r0_after_load);
  fault_puts("\n  r2_addr=");
  fault_put_hex(pendsv_debug_r2_addr);
  fault_puts("\n  psp_after_set=");
  fault_put_hex(pendsv_debug_psp_after_set);
  fault_puts("\n");

  /* Print actual PSP for comparison */
  uint32_t actual_psp;
  __asm volatile("mrs %0, psp" : "=r"(actual_psp));
  fault_puts("  actual_psp=");
  fault_put_hex(actual_psp);
  fault_puts("\n");
#endif

  /* DESIGN DECISION: Hard-halt on bus faults (see MemManage handler for
   * rationale) */
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
static void __attribute__((used)) UsageFault_Handler_C(uint32_t *frame) {
  uint8_t partition_id __attribute__((unused));
  uint32_t cfsr;

  pok_cons_write("!!! USAGEFAULT !!!\n", 19);

  if (frame == NULL) {
    pok_cons_write("FAULT: NULL frame\n", 18);
    __disable_irq();
    while (1) {
      __asm volatile("wfi");
    }
  }

  partition_id = pok_current_partition;

  /* Read CFSR to check fault status */
  cfsr = SCB_CFSR;

  /* Output CFSR value */
  pok_cons_write("CFSR=0x", 7);
  char hex[9];
  uint32_t temp = cfsr;
  for (int i = 7; i >= 0; i--) {
    hex[i] = "0123456789ABCDEF"[temp & 0xF];
    temp >>= 4;
  }
  hex[8] = '\n';
  pok_cons_write(hex, 9);

  /* Output PC and LR */
  pok_cons_write("PC=0x", 5);
  temp = frame[6];
  for (int i = 7; i >= 0; i--) {
    hex[i] = "0123456789ABCDEF"[temp & 0xF];
    temp >>= 4;
  }
  hex[8] = ' ';
  pok_cons_write(hex, 9);

  pok_cons_write("LR=0x", 5);
  temp = frame[5];
  for (int i = 7; i >= 0; i--) {
    hex[i] = "0123456789ABCDEF"[temp & 0xF];
    temp >>= 4;
  }
  hex[8] = '\n';
  pok_cons_write(hex, 9);

  /* Clear Usage fault flags in CFSR using write-1-to-clear semantics
   * Only clear the UFSR bits that are actually set to avoid affecting
   * MMFSR/BFSR */
  SCB_CFSR = cfsr & ARM_CFSR_UFSR_MASK; /* Write 1s to clear set UFSR bits */

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
  uint32_t pc = frame[6] & ~1; /* Clear Thumb bit */
  if (pok_bsp_flash_base != 0 && pok_bsp_flash_size != 0 &&
      pc >= pok_bsp_flash_base &&
      pc < (pok_bsp_flash_base + pok_bsp_flash_size)) { /* Within Flash range */
    uint16_t instruction = *((volatile uint16_t *)pc);
    fault_put_hex(instruction);
  } else {
    fault_puts("(invalid PC)");
  }
  fault_puts("\n");

  /* Debug: Print PSP and thread stack for analysis */
  uint32_t psp_val;
  __asm volatile("mrs %0, psp" : "=r"(psp_val));
  fault_puts("PSP=");
  fault_put_hex(psp_val);
  fault_puts("\n");

  /* Print PSP stack contents */
  fault_puts("PSP Stack: ");
  for (int i = 0; i < 8; i++) {
    if ((psp_val + i * 4) >= 0x20000000 && (psp_val + i * 4) < 0x20020000) {
      fault_put_hex(*(uint32_t *)(psp_val + i * 4));
      fault_puts(" ");
    }
  }
  fault_puts("\n");

  /* Print thread 1 SP and context frame */
  extern pok_thread_t pok_threads[];
  fault_puts("Thread1 SP=");
  fault_put_hex(pok_threads[1].sp);
  uint32_t t1_sp = pok_threads[1].sp;
  fault_puts("\nThread1 SW frame [r4-r11] at ");
  fault_put_hex(t1_sp - 32);
  fault_puts(": ");
  for (int i = 0; i < 8; i++) {
    uint32_t addr = (t1_sp - 32) + i * 4;
    if (addr >= 0x20000000 && addr < 0x20020000) {
      fault_put_hex(*(uint32_t *)addr);
      fault_puts(" ");
    }
  }
  fault_puts("\nThread1 HW frame [r0-xpsr] at ");
  fault_put_hex(t1_sp);
  fault_puts(": ");
  for (int i = 0; i < 8; i++) {
    uint32_t addr = t1_sp + i * 4;
    if (addr >= 0x20000000 && addr < 0x20020000) {
      fault_put_hex(*(uint32_t *)addr);
      fault_puts(" ");
    }
  }
  fault_puts("\n");

  /* Print thread 2 SP and stack */
  fault_puts("Thread2 SP=");
  fault_put_hex(pok_threads[2].sp);
  fault_puts(" Stack: ");
  uint32_t t2_sp = pok_threads[2].sp;
  for (int i = -8; i < 8; i++) {
    uint32_t addr = t2_sp + i * 4;
    if (addr >= 0x20000000 && addr < 0x20020000) {
      if (i == 0)
        fault_puts("[");
      fault_put_hex(*(uint32_t *)addr);
      if (i == 0)
        fault_puts("]");
      fault_puts(" ");
    }
  }
  fault_puts("\n");

  /* Print PendSV debug values */
  extern volatile uint32_t g_debug_loaded_sp;
  extern volatile uint32_t g_debug_final_psp;
  extern volatile uint32_t g_debug_saved_from_msp;
  extern volatile uint32_t g_debug_sw_frame_addr;
  extern volatile uint32_t g_debug_ctx_pc;
  extern volatile uint32_t g_debug_ctx_xpsr;
  fault_puts("CTX_SAVED: pc=");
  fault_put_hex(g_debug_ctx_pc);
  fault_puts(" xpsr=");
  fault_put_hex(g_debug_ctx_xpsr);
  fault_puts("\n");
  fault_puts("PendSV: loaded_sp=");
  fault_put_hex(g_debug_loaded_sp);
  fault_puts(" final_psp=");
  fault_put_hex(g_debug_final_psp);
  fault_puts(" saved_from_msp=");
  fault_put_hex(g_debug_saved_from_msp);
  fault_puts(" sw_frame=");
  fault_put_hex(g_debug_sw_frame_addr);
  fault_puts("\n");
#endif

  /* DESIGN DECISION: Hard-halt on usage faults (see MemManage handler for
   * rationale) */
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
static void __attribute__((used)) HardFault_Handler_C(uint32_t *frame) {
  uint8_t partition_id __attribute__((unused));

  pok_cons_write("!!! HARDFAULT !!!\n", 18);

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
