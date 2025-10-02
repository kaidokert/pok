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

#ifndef __POK_SPINLOCK_H__
#define __POK_SPINLOCK_H__

#include <assert.h>

typedef unsigned int pok_spinlock_t;

/* ARM Cortex-M is single-core, so spinlocks are just critical sections */
#define SPIN_UNLOCK(_spin_)                                                    \
  do {                                                                         \
    assert(_spin_);                                                            \
    (_spin_) = 0;                                                              \
    __asm volatile("cpsie i" : : : "memory");                                  \
  } while (0)

#define SPIN_LOCK(_spin_)                                                      \
  do {                                                                         \
    __asm volatile("cpsid i" : : : "memory");                                  \
    (_spin_) = 1;                                                              \
  } while (0)

#define IS_LOCK(_spin_) (_spin_ == 1)

#endif /* !__POK_SPINLOCK_H__ */
