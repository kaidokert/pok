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

#include <core/thread.h>
#include <errno.h>

#ifdef POK_NEEDS_BLACKBOARDS
#include <middleware/blackboard.h>
pok_ret_t pok_blackboard_init(void);
#endif

#ifdef POK_NEEDS_BUFFERS
#include <middleware/buffer.h>
pok_ret_t pok_buffer_init(void);
#endif

int main();

int __pok_partition_start() {
#ifdef POK_NEEDS_MIDDLEWARE

#ifdef POK_NEEDS_BLACKBOARDS
  pok_blackboard_init();
#endif

#ifdef POK_NEEDS_BUFFERS
  pok_buffer_init();
#endif

#endif    /* POK_NEEDS_MIDDLEWARE */
  main(); /* main loop from user */

  /* Never return - terminate thread via syscall to avoid trying to jump to
   * kernel address. Partitions run in unprivileged mode with MPU protection
   * and cannot access kernel memory where the thread exit stub resides. */
  pok_thread_stop_self();

  /* Should never reach here, but loop forever if syscall fails */
  while (1) {
    /* Wait for interrupt */
  }

  return (0); /* Keep return for compiler */
}
