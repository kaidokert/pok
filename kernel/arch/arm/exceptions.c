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

#include "nvic.h"
#include "mpu.h"
#include <core/debug.h>
#include <core/partition.h>
#include <errno.h>

/*
 * Memory Management Fault Handler
 * Handles MPU violations and other memory management faults
 */
void MemManage_Handler(void) {
  uint32_t *frame;
  uint32_t fault_addr;
  uint8_t partition_id;
  
  /* Get stack frame */
  __asm volatile ("mrs %0, psp" : "=r" (frame));
  
  /* Get faulting address from MemManage Fault Address Register */
  fault_addr = *((volatile uint32_t *)(SCB_BASE + 0x34)); /* MMFAR */
  
  /* Get current partition */
  extern uint8_t pok_current_partition;
  partition_id = pok_current_partition;
  
#ifdef POK_NEEDS_DEBUG
  printf("MemManage fault in partition %d at address 0x%x\n", 
         partition_id, fault_addr);
  printf("PC: 0x%x, LR: 0x%x\n", frame[6], frame[5]);
#endif
  
  /* Handle partition isolation violation */
  if (partition_id < POK_CONFIG_NB_PARTITIONS) {
    /* Terminate the offending partition */
    pok_partition_set_mode(partition_id, POK_PARTITION_MODE_STOPPED);
    
    /* Force a reschedule to switch away from this partition */
    pok_sched();
  }
  
  /* If we reach here, halt the system */
  while (1) {
    __asm volatile ("wfi");
  }
}

/*
 * Bus Fault Handler
 * Handles bus errors and invalid memory accesses
 */
void BusFault_Handler(void) {
  uint32_t *frame;
  uint32_t fault_addr;
  uint8_t partition_id;
  
  __asm volatile ("mrs %0, psp" : "=r" (frame));
  
  /* Get faulting address from Bus Fault Address Register */
  fault_addr = *((volatile uint32_t *)(SCB_BASE + 0x38)); /* BFAR */
  
  extern uint8_t pok_current_partition;
  partition_id = pok_current_partition;
  
#ifdef POK_NEEDS_DEBUG
  printf("BusFault in partition %d at address 0x%x\n", 
         partition_id, fault_addr);
  printf("PC: 0x%x, LR: 0x%x\n", frame[6], frame[5]);
#endif
  
  if (partition_id < POK_CONFIG_NB_PARTITIONS) {
    pok_partition_set_mode(partition_id, POK_PARTITION_MODE_STOPPED);
    pok_sched();
  }
  
  while (1) {
    __asm volatile ("wfi");
  }
}

/*
 * Usage Fault Handler  
 * Handles undefined instruction, unaligned access, etc.
 */
void UsageFault_Handler(void) {
  uint32_t *frame;
  uint8_t partition_id;
  
  __asm volatile ("mrs %0, psp" : "=r" (frame));
  
  extern uint8_t pok_current_partition;
  partition_id = pok_current_partition;
  
#ifdef POK_NEEDS_DEBUG
  printf("UsageFault in partition %d\n", partition_id);
  printf("PC: 0x%x, LR: 0x%x\n", frame[6], frame[5]);
#endif
  
  if (partition_id < POK_CONFIG_NB_PARTITIONS) {
    pok_partition_set_mode(partition_id, POK_PARTITION_MODE_STOPPED);
    pok_sched();
  }
  
  while (1) {
    __asm volatile ("wfi");
  }
}

/*
 * Hard Fault Handler
 * Last resort fault handler
 */
void HardFault_Handler(void) {
  uint32_t *frame;
  uint8_t partition_id;
  
  __asm volatile ("mrs %0, psp" : "=r" (frame));
  
  extern uint8_t pok_current_partition;
  partition_id = pok_current_partition;
  
#ifdef POK_NEEDS_DEBUG
  printf("HardFault in partition %d\n", partition_id);
  printf("PC: 0x%x, LR: 0x%x, PSR: 0x%x\n", frame[6], frame[5], frame[7]);
  printf("r0: 0x%x, r1: 0x%x, r2: 0x%x, r3: 0x%x\n", 
         frame[0], frame[1], frame[2], frame[3]);
#endif
  
  /* Try to recover by stopping the current partition */
  if (partition_id < POK_CONFIG_NB_PARTITIONS) {
    pok_partition_set_mode(partition_id, POK_PARTITION_MODE_STOPPED);
    pok_sched();
  }
  
  /* If recovery fails, halt the system */
  while (1) {
    __asm volatile ("wfi");
  }
}