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

#if defined(POK_NEEDS_PORTS_QUEUEING) || defined(POK_NEEDS_PORTS_SAMPLING)

#include <libc.h>
#include <middleware/queue.h>

extern pok_queue_t pok_queue;
extern char pok_queue_data[1024];

void pok_queue_init() {
#ifdef POK_NEEDS_DEBUG
  printf("[QUEUE_INIT] Initializing queue: size=1024, available=1024\n");
#endif
  pok_queue.size = 1024;
  pok_queue.available_size = 1024;
  pok_queue.data = pok_queue_data;
#ifdef POK_NEEDS_DEBUG
  printf("[QUEUE_INIT] After init: pok_queue.size=%d, "
         "pok_queue.available_size=%d\n",
         pok_queue.size, pok_queue.available_size);
#endif
}
#endif
