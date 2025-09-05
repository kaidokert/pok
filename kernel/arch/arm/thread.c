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

/* POK system headers */
#include <errno.h>
#include <libc.h>

/* POK core headers */
#include <bsp.h>
#include <core/thread.h>

/* Architecture-specific headers */
#include "arch.h"
#include "cortex_m_config.h"
#include "nvic.h"
#include "thread.h"

#define STACK_ALIGNMENT CORTEX_M_STACK_ALIGNMENT
#define STACK_ALIGNMENT_MASK CORTEX_M_STACK_ALIGNMENT_MASK

/**
 * Create a thread context with proper ARM Cortex-M stack frame
 *
 * @param thread_id Unique identifier for the thread
 * @param stack_size Size of stack to allocate in bytes
 * @param entry Entry point function address for the thread
 * @return Context pointer on success, 0 on failure
 */
uint32_t pok_context_create(uint32_t thread_id, uint32_t stack_size,
                            uintptr_t entry) {
  start_context_t *sp;
  char *stack_addr;

  stack_addr = pok_bsp_mem_alloc(stack_size);
  if (!stack_addr) {
    return (0);
  }

  /* Place context at top of stack */
  sp = (start_context_t *)(stack_addr + stack_size - sizeof(start_context_t));

  memset(sp, 0, sizeof(start_context_t));

  /* Initialize context for thread startup */
  sp->ctx.pc = (uint32_t)pok_arch_thread_start; /* Start with thread wrapper */
  sp->ctx.lr = ARM_EXC_RETURN_THREAD_PSP; /* Return to Thread mode, use PSP */
  sp->ctx.xpsr = 0x01000000; /* Thumb bit set */
  /* Ensure 8-byte aligned stack pointer */
  sp->ctx.sp = ((uint32_t)stack_addr + stack_size - STACK_ALIGNMENT) &
               ~STACK_ALIGNMENT_MASK;

  sp->entry = entry;
  sp->id = thread_id;

  return ((uint32_t)sp);
}

/* Global variables for PendSV context switching - accessed by PendSV handler */
uint32_t * volatile g_old_sp_ptr = NULL;
volatile uint32_t g_new_sp = 0;
/**
 * Perform ARM Cortex-M context switch between threads
 *
 * Uses PendSV exception for proper atomic context switching.
 * This function sets up the context switch parameters and triggers PendSV.
 * The actual context switch happens in the PendSV handler.
 *
 * @param old_sp Pointer to store current thread's stack pointer
 * @param new_sp Stack pointer of thread to switch to
 */
void pok_context_switch(uint32_t *old_sp, uint32_t new_sp) {
  if (old_sp == NULL) {
    return;
  }

  /* Set up context switch parameters for PendSV handler */
  g_old_sp_ptr = old_sp;
  g_new_sp = new_sp;

  /* Ensure memory operations complete before triggering PendSV */
  __asm volatile("dsb" ::: "memory");

  /* Trigger PendSV exception to perform context switch */
  SCB_ICSR |= SCB_ICSR_PENDSVSET;

  /* Memory barrier to ensure PendSV is triggered */
  __asm volatile("dsb; isb" ::: "memory");
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

  sp->ctx.pc = (uint32_t)pok_arch_thread_start;
  sp->ctx.lr = ARM_EXC_RETURN_THREAD_PSP;
  sp->ctx.xpsr = 0x01000000;
  /* Ensure 8-byte aligned stack pointer */
  sp->ctx.sp = ((stack_addr + stack_size - STACK_ALIGNMENT) & ~STACK_ALIGNMENT_MASK);

  sp->entry = entry;
  sp->id = id;
}

/*
 * Thread startup wrapper
 * This function is called when a new thread starts execution
 */
void pok_arch_thread_start(void) {
  start_context_t *ctx;
  uint32_t entry, thread_id;

  /* Get current context from PSP */
  __asm volatile("mrs %0, psp" : "=r"(ctx));

  /* Extract thread information */
  entry = ctx->entry;
  thread_id = ctx->id;

  /* Call POK core thread start function */
  pok_thread_start((void (*)(void))entry, thread_id);
}
