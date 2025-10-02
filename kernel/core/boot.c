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
 * \file    core/boot.c
 * \author  Julien Delange
 * \brief   Boot function to start the kernel
 * \date    2008-2009
 */

#include <arch.h>
#include <bsp.h>

#include <core/boot.h>
#include <core/partition.h>
#include <core/sched.h>
#include <core/thread.h>
#include <core/time.h>
#include <middleware/port.h>
#include <middleware/queue.h>

#include <core/instrumentation.h>
#include <libc.h>

void pok_boot() {
  pok_arch_init();
  pok_bsp_init();
  pok_partition_init();
  pok_thread_init();
  pok_sched_init(); /* Initialize scheduler BEFORE starting timer */
  pok_time_init();  /* Initialize timer last - starts SysTick interrupts */

#if (defined POK_NEEDS_LOCKOBJ) || defined(POK_NEEDS_PORTS_QUEUEING) ||        \
    defined(POK_NEEDS_PORTS_SAMPLING)
  pok_lockobj_init();
#endif
#if defined(POK_NEEDS_PORTS_QUEUEING) || defined(POK_NEEDS_PORTS_SAMPLING)
  pok_port_init();
  pok_queue_init();
#endif

#if defined(POK_NEEDS_DEBUG) || defined(POK_NEEDS_CONSOLE)
  pok_cons_write("POK kernel initialized\n", 23);
#endif

  /* ARM Cortex-M: Let partition main threads create resources in INIT mode
   * Partitions will call pok_partition_set_mode() themselves when ready */
#if defined(POK_NEEDS_DEBUG) || defined(POK_NEEDS_CONSOLE)
  pok_cons_write(
      "Partitions starting in INIT mode (will transition via syscall)\n", 64);
#endif

  /* Do NOT automatically transition to NORMAL mode - let partitions do it */
  /* for (uint8_t i = 0; i < POK_CONFIG_NB_PARTITIONS; i++) {
    pok_partition_set_mode(i, POK_PARTITION_MODE_NORMAL);
  } */

#ifdef POK_NEEDS_INSTRUMENTATION
  uint32_t tmp;
  printf("[INSTRUMENTATION][CHEDDAR] <event_table>\n");
  printf("[INSTRUMENTATION][CHEDDAR] <processor>\n");
  printf("[INSTRUMENTATION][CHEDDAR] <name>pok_kernel</name>\n");

  for (tmp = 0; tmp < POK_CONFIG_NB_THREADS; tmp++) {
    printf("[INSTRUMENTATION][CHEDDAR] <task_activation>   0   task "
           "%d</task_activation>\n",
           tmp);
  }
#endif

  pok_arch_preempt_enable();

  pok_arch_idle();
}
