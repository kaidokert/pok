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

#ifndef __POK_ARM_THREAD_H__
#define __POK_ARM_THREAD_H__

#include <types.h>

/*
 * ARM Cortex-M context structure
 * This represents the CPU state that must be saved/restored during context
 * switches
 */
typedef struct {
  /* Registers saved by hardware on exception entry */
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;   /* Link register */
  uint32_t pc;   /* Program counter */
  uint32_t xpsr; /* Program status register */

  /* Registers saved manually by software */
  uint32_t r4;
  uint32_t r5;
  uint32_t r6;
  uint32_t r7;
  uint32_t r8;
  uint32_t r9;
  uint32_t r10;
  uint32_t r11;
  uint32_t sp; /* Stack pointer (PSP for threads) */
} context_t;

/*
 * Thread startup context structure
 */
typedef struct {
  context_t ctx;
  uint32_t entry; /* Thread entry point */
  uint32_t id;    /* Thread ID */
} start_context_t;

/* Function prototypes */
uint32_t pok_context_create(uint32_t thread_id, uint32_t stack_size,
                            uint32_t entry);
void pok_context_switch(uint32_t *old_sp, uint32_t new_sp);
void pok_context_reset(uint32_t stack_size, uint32_t stack_addr);
void pok_arch_thread_start(void);

#endif /* !__POK_ARM_THREAD_H__ */