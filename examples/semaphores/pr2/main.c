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

#include "activity.h"
#include <core/partition.h>
#include <core/thread.h>
#include <libc/stdio.h>
#include <types.h>

int main() {
  uint32_t tid; /* libpok API uses uint32_t */
  pok_ret_t ret;
  pok_thread_attr_t tattr;

  printf("=== POK Semaphores Demo - Partition 2 ===\n");

  tattr.priority = 42;
  tattr.entry = pinger_job;
  tattr.stack_size = 2048;
  tattr.period = 0;
  tattr.deadline = 0;
  tattr.time_capacity = 0;
  tattr.processor_affinity = 0;

  ret = pok_thread_create(&tid, &tattr);
  printf("[P2] thread create returns=%d\n", ret);

  printf("[P2] Main thread switching partition to NORMAL mode\n");
  ret = pok_partition_set_mode(POK_PARTITION_MODE_NORMAL);
  printf("[P2] pok_partition_set_mode return=%d\n", ret);

  printf("[P2] Main thread entering infinite loop (worker thread should now "
         "execute)\n");

  while (1)
    ; /* Main thread idle - worker thread will execute */

  return (0);
}
