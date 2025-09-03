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

#include <stddef.h>
#include <types.h>

/*
 * ARM Cortex-M context structure for PendSV context switching
 *
 * LAYOUT CRITICAL: Order must match PendSV handler in syscalls.c
 *
 * PendSV Handler Flow:
 * 1. mrs r0, psp              <- Get current PSP
 * 2. stmdb r0!, {r4-r11}      <- Push r4-r11 onto thread stack
 * 3. Store r0 to old thread   <- r0 now points after saved r4-r11
 * 4. ldmia r0!, {r4-r11}      <- Pop r4-r11 from new thread stack
 * 5. msr psp, r0              <- Set PSP to point after r4-r11
 * 6. Hardware pops r0-r3,r12,lr,pc,xpsr from PSP stack on return
 */
/* ARM Cortex-M context structure with proper alignment for PendSV context
 * switching CRITICAL: Structure must maintain 8-byte alignment for stack
 * operations while ensuring PendSV register layout compatibility
 */
typedef struct __attribute__((aligned(8))) {
  /* SOFTWARE-SAVED: PendSV saves these manually (MUST BE FIRST) */
  uint32_t r4;
  uint32_t r5;
  uint32_t r6;
  uint32_t r7;
  uint32_t r8;
  uint32_t r9;
  uint32_t r10;
  uint32_t r11;

  /* HARDWARE-SAVED: Exception hardware pushes these onto PSP stack */
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;   /* Link register */
  uint32_t pc;   /* Program counter */
  uint32_t xpsr; /* Program status register */

  /* No PSP field needed - PSP is managed externally by context switcher */
} context_t;

/* Layout asserts to verify structure integrity and freeze invariants */
_Static_assert(sizeof(context_t) == 64,
               "context_t size must be exactly 64 bytes");
_Static_assert(_Alignof(context_t) >= 8,
               "context_t alignment must be at least 8 bytes");
_Static_assert(sizeof(context_t) % 8 == 0, "context_t must be 8-byte aligned");

/* Freeze critical offset invariants for assembly code compatibility */
_Static_assert(offsetof(context_t, r4) == 0, "r4 must be at offset 0");
_Static_assert(offsetof(context_t, r5) == 4, "r5 must be at offset 4");
_Static_assert(offsetof(context_t, r6) == 8, "r6 must be at offset 8");
_Static_assert(offsetof(context_t, r7) == 12, "r7 must be at offset 12");
_Static_assert(offsetof(context_t, r8) == 16, "r8 must be at offset 16");
_Static_assert(offsetof(context_t, r9) == 20, "r9 must be at offset 20");
_Static_assert(offsetof(context_t, r10) == 24, "r10 must be at offset 24");
_Static_assert(offsetof(context_t, r11) == 28, "r11 must be at offset 28");
_Static_assert(offsetof(context_t, r0) == 32, "r0 must be at offset 32");
_Static_assert(offsetof(context_t, r1) == 36, "r1 must be at offset 36");
_Static_assert(offsetof(context_t, r2) == 40, "r2 must be at offset 40");
_Static_assert(offsetof(context_t, r3) == 44, "r3 must be at offset 44");
_Static_assert(offsetof(context_t, r12) == 48, "r12 must be at offset 48");
_Static_assert(offsetof(context_t, lr) == 52, "lr must be at offset 52");
_Static_assert(offsetof(context_t, pc) == 56, "pc must be at offset 56");
_Static_assert(offsetof(context_t, xpsr) == 60, "xpsr must be at offset 60");

/*
 * Thread startup context structure - also needs proper alignment
 */
typedef struct __attribute__((aligned(8))) {
  context_t ctx;
  uint32_t entry; /* Thread entry point */
  uint32_t id;    /* Thread ID */
} start_context_t;

/* Function prototypes */
uint32_t pok_context_create(uint32_t thread_id, uint32_t stack_size,
                            uintptr_t entry);
void pok_context_switch(uint32_t *old_sp, uint32_t new_sp);
void pok_context_reset(uint32_t stack_size, uint32_t stack_addr);
void pok_arch_thread_start(start_context_t *ctx);

#endif /* !__POK_ARM_THREAD_H__ */
