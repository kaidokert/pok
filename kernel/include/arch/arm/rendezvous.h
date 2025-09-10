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

#ifndef __POK_RENDEZVOUS_H__
#define __POK_RENDEZVOUS_H__

#include <assert.h>

/* ARM Cortex-M is single-core, so rendezvous operations are stubs */
typedef volatile unsigned int *rendezvous_t;
typedef volatile unsigned int rendezvous;

static inline void start_rendezvous(rendezvous_t r) { (void)r; }

static inline void spin_wait_for_rendezvous(rendezvous_t r,
                                            unsigned int others_count) {
  (void)r;
  (void)others_count;
}

static inline void unblock_rendezvous(rendezvous_t r) { (void)r; }

static inline void join_rendezvous(rendezvous_t r) { (void)r; }

#endif /* __POK_RENDEZVOUS_H__ */
