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
 * \file    thread.c
 * \brief   ARM Cortex-M thread management and context switching
 * \author  POK team
 */

#include <bsp.h>
#include <core/thread.h>
#include <errno.h>
#include <libc.h>

#include "thread.h"

uint32_t pok_context_create(uint32_t thread_id, uint32_t stack_size,
                            uint32_t entry) {
  start_context_t *sp;
  char *stack_addr;

  stack_addr = pok_bsp_mem_alloc(stack_size);
  if (!stack_addr) {
    return 0;
  }

  /* Place context at top of stack */
  sp = (start_context_t *)(stack_addr + stack_size - sizeof(start_context_t));

  memset(sp, 0, sizeof(start_context_t));

  /* Initialize context for thread startup */
  sp->ctx.pc = (uint32_t)pok_thread_start;  /* Start with thread wrapper */
  sp->ctx.lr = 0xFFFFFFFD;                  /* Return to Thread mode, use PSP */
  sp->ctx.xpsr = 0x01000000;                /* Thumb bit set */
  sp->ctx.sp = (uint32_t)stack_addr + stack_size - 8;  /* User stack pointer */
  
  sp->entry = entry;
  sp->id = thread_id;

  return ((uint32_t)sp);
}

/*
 * Context switch implementation using PendSV exception
 * This is written in inline assembly to have precise control over register usage
 */
void pok_context_switch(uint32_t *old_sp, uint32_t new_sp) {
  __asm volatile (
    /* Disable interrupts */
    "cpsid i                    \n"
    
    /* Save current context */
    "mrs r2, psp                \n"  /* Get current PSP */
    "stmdb r2!, {r4-r11}        \n"  /* Save r4-r11 to stack */
    "str r2, [%0]               \n"  /* Store new PSP to old_sp */
    
    /* Load new context */
    "mov r2, %1                 \n"  /* Load new PSP */
    "ldmia r2!, {r4-r11}        \n"  /* Restore r4-r11 from stack */
    "msr psp, r2                \n"  /* Set new PSP */
    
    /* Re-enable interrupts */
    "cpsie i                    \n"
    
    /* Trigger PendSV to complete context switch */
    "ldr r2, =0xE000ED04        \n"  /* SCB->ICSR */
    "ldr r3, =0x10000000        \n"  /* PENDSVSET bit */
    "str r3, [r2]               \n"  /* Trigger PendSV */
    
    : /* no output */
    : "r" (old_sp), "r" (new_sp)
    : "r2", "r3", "memory"
  );
}

void pok_context_reset(uint32_t stack_size, uint32_t stack_addr) {
  start_context_t *sp;
  uint32_t id;
  uint32_t entry;

  sp = (start_context_t *)(stack_addr + stack_size - sizeof(start_context_t));

  /* Preserve thread information */
  id = sp->id;
  entry = sp->entry;
  
  /* Reset context */
  memset(sp, 0, sizeof(start_context_t));
  
  sp->ctx.pc = (uint32_t)pok_thread_start;
  sp->ctx.lr = 0xFFFFFFFD;
  sp->ctx.xpsr = 0x01000000;
  sp->ctx.sp = stack_addr + stack_size - 8;
  
  sp->entry = entry;
  sp->id = id;
}

/*
 * Thread startup wrapper
 * This function is called when a new thread starts execution
 */
void pok_thread_start(void) {
  start_context_t *ctx;
  uint32_t entry, thread_id;
  
  /* Get current context from PSP */
  __asm volatile ("mrs %0, psp" : "=r" (ctx));
  
  /* Extract thread information */
  entry = ctx->entry;
  thread_id = ctx->id;
  
  /* Call the actual thread entry point */
  ((void (*)(void))entry)();
  
  /* Thread should never return, but if it does, terminate it */
  pok_thread_stop_self();
}