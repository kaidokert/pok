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
#include <core/memcheck.h>
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
  pok_sched_init(); /* Initialize scheduler structures */

  /* Verify memory layout - register all memory regions and check for overlaps
   */
  pok_memcheck_init();

  /* Register kernel region */
#ifdef POK_ARCH_ARM
  /* ARM uses fixed memory layout defined in BSP */
  extern uintptr_t pok_bsp_kernel_base(void);
  extern size_t pok_bsp_kernel_size(void);
  pok_memcheck_register_region("Kernel", (uint32_t)pok_bsp_kernel_base(),
                               (uint32_t)pok_bsp_kernel_size());
#endif

  /* Register each partition region */
  for (uint8_t i = 0; i < POK_CONFIG_NB_PARTITIONS; i++) {
    /* Use simple buffer instead of snprintf to avoid libc dependency */
    char name[] = "Partition X";
    name[10] = '0' + i; /* Works for partitions 0-9 */
    pok_memcheck_register_region(name, pok_partitions[i].base_addr,
                                 pok_partitions[i].size);
  }

  /* Register heap region */
#ifdef POK_ARCH_ARM
  extern uint32_t pok_arm_pm_heap_start;
  extern uint32_t pok_arm_pm_heap_end;
  pok_memcheck_register_region("Heap", pok_arm_pm_heap_start,
                               pok_arm_pm_heap_end - pok_arm_pm_heap_start);
#endif

  /* Verify no overlaps - will trigger kernel panic if overlap detected */
  pok_memcheck_verify_layout();

  /* Initialize middleware BEFORE starting timer - timer enables interrupts
   * which activate scheduler and context switch to partitions */
#if (defined POK_NEEDS_LOCKOBJ) || defined(POK_NEEDS_PORTS_QUEUEING) ||        \
    defined(POK_NEEDS_PORTS_SAMPLING)
  pok_lockobj_init();
#endif
#if defined(POK_NEEDS_PORTS_QUEUEING) || defined(POK_NEEDS_PORTS_SAMPLING)
#ifdef POK_NEEDS_DEBUG
  printf("[BOOT] About to call pok_port_init() and pok_queue_init()\n");
#endif
  pok_port_init();
  pok_queue_init();
#ifdef POK_NEEDS_DEBUG
  printf("[BOOT] Finished calling pok_port_init() and pok_queue_init()\n");
#endif
#endif

  /* ARM Cortex-M: Manually start first thread BEFORE enabling timer
   * This ensures we enter thread mode before any interrupts fire */
#ifdef POK_ARCH_ARM
  extern void pok_arch_start_first_thread(void);
#ifdef POK_NEEDS_DEBUG
  printf("[BOOT] Starting first thread before timer init\n");
#endif
  pok_arch_start_first_thread(); /* Does not return - starts threads */
#else
  pok_time_init(); /* Initialize timer AFTER middleware - starts SysTick
                      interrupts */

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
#endif

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
