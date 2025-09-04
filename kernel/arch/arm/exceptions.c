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

#include "mpu.h"
#include "nvic.h"
#include <core/debug.h>
#include <core/partition.h>
#include <errno.h>

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
  SCB_CFSR = cfsr & 0xFF; /* Clear MMFSR bits */

  /* Get current partition */
  extern uint8_t pok_current_partition;
  partition_id = pok_current_partition;

#ifdef POK_NEEDS_DEBUG
  if (cfsr & CFSR_MMARVALID) {
    printf("MemManage fault in partition %d at address 0x%x\n", partition_id,
           fault_addr);
  } else {
    printf("MemManage fault in partition %d (address not available)\n",
           partition_id);
  }
  printf("PC: 0x%x, LR: 0x%x, CFSR: 0x%x\n", frame[6], frame[5], cfsr);
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
  SCB_CFSR = (cfsr & 0xFF00) >> 8; /* Clear BFSR bits */

  extern uint8_t pok_current_partition;
  partition_id = pok_current_partition;

#ifdef POK_NEEDS_DEBUG
  if (cfsr & CFSR_BFARVALID) {
    printf("BusFault in partition %d at address 0x%x\n", partition_id,
           fault_addr);
  } else {
    printf("BusFault in partition %d (address not available)\n", partition_id);
  }
  printf("PC: 0x%x, LR: 0x%x, CFSR: 0x%x\n", frame[6], frame[5], cfsr);
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

#ifdef POK_NEEDS_DEBUG
  printf("UsageFault in partition %d\n", partition_id);
  printf("PC: 0x%x, LR: 0x%x\n", frame[6], frame[5]);
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
  printf("HardFault in partition %d\n", partition_id);
  printf("PC: 0x%x, LR: 0x%x, PSR: 0x%x\n", frame[6], frame[5], frame[7]);
  printf("r0: 0x%x, r1: 0x%x, r2: 0x%x, r3: 0x%x\n", frame[0], frame[1],
         frame[2], frame[3]);
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