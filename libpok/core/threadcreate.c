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

#include <core/dependencies.h>

#include <arch.h>
#include <core/syscall.h>
#include <core/thread.h>
#include <libc/stdio.h>
#include <types.h>

pok_ret_t pok_thread_create(uint32_t *thread_id,
                            const pok_thread_attr_t *attr) {
#ifdef POK_NEEDS_DEBUG
  printf("[USERSPACE] pok_thread_create: attr=%p, attr->period=%llu (0x%llx)\n",
         attr, (unsigned long long)attr->period,
         (unsigned long long)attr->period);
  // Read the raw memory at attr to see the actual bytes
  uint64_t *period_ptr = (uint64_t *)&attr->period;
  printf("[USERSPACE] Raw period bytes at %p: 0x%016llx\n", period_ptr,
         (unsigned long long)*period_ptr);
#endif
  return pok_syscall2(POK_SYSCALL_THREAD_CREATE, (uint32_t)thread_id,
                      (uint32_t)attr);
}
