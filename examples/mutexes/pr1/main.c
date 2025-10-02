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
#include <core/mutex.h>
#include <core/partition.h>
#include <core/thread.h>
#include <libc/stdio.h>
#include <types.h>

uint8_t mid;

int main() {
  uint32_t tid; /* Changed from uint8_t to match pok_thread_create signature */
  pok_ret_t ret;
  pok_thread_attr_t tattr;

  /* CRITICAL DEBUG: Use magic values to prove main() is executing */
  volatile uint32_t debug_magic = 0xDEADBEEF;
  volatile uint32_t debug_entry = 0x12345678;
  (void)debug_magic;
  (void)debug_entry;

  printf("=== POK Mutexes Demo - Partition 1 ===\n");

  /* Create the mutex first */
  ret = pok_mutex_create(&mid, POK_QUEUEING_DISCIPLINE_FIFO,
                         POK_LOCKOBJ_POLICY_STANDARD);
  printf("[P1] pok_mutex_create return=%d, mid=%d\n", ret, mid);

  /* Set up thread attributes for first worker thread */
  tattr.priority = 44;
  tattr.entry = pinger_job;
  tattr.stack_size = 2048;
  tattr.period = 0;
  tattr.deadline = 0;
  tattr.time_capacity = 0;
  tattr.processor_affinity = 0;

  ret = pok_thread_create(&tid, &tattr);
  printf("[P1] pok_thread_create (1) return=%d\n", ret);

  /* Set up thread attributes for second worker thread */
  tattr.priority = 42;
  tattr.entry = pinger_job;
  tattr.stack_size = 2048;

  ret = pok_thread_create(&tid, &tattr);
  printf("[P1] pok_thread_create (2) return=%d\n", ret);

  printf("[P1] Main thread switching partition to NORMAL mode\n");
  ret = pok_partition_set_mode(POK_PARTITION_MODE_NORMAL);
  printf("[P1] pok_partition_set_mode return=%d\n", ret);

  printf("[P1] Main thread entering infinite loop (worker threads should now "
         "execute)\n");

  while (1)
    ; /* Main thread idle - worker threads will execute */

  return (0);
}
