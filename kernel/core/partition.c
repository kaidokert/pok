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
 * \file partition.c
 * \brief This file provides functions for partitioning services
 * \author Julien Delange
 */

#include <arch.h>
#include <bsp.h>
#include <core/debug.h>
#include <core/error.h>
#include <core/instrumentation.h>
#include <core/loader.h>
#include <core/multiprocessing.h>
#include <core/partition.h>
#include <core/sched.h>
#include <core/thread.h>
#include <core/time.h>

#include <dependencies.h>
#include <errno.h>

#include <libc.h>

/**
 * \brief The array that contains ALL partitions in the system.
 */
pok_partition_t pok_partitions[POK_CONFIG_NB_PARTITIONS];
uint32_t current_threads[POK_CONFIG_NB_PROCESSORS];

uint8_t pok_partitions_index = 0;

extern uint64_t pok_sched_slots[];
extern uint64_t partition_processor_affinity[];

/**
 **\brief Setup the scheduler used in partition pid
 */
void pok_partition_setup_scheduler(const uint8_t pid) {
#ifdef POK_CONFIG_PARTITIONS_SCHEDULER
  switch (((pok_sched_t[])POK_CONFIG_PARTITIONS_SCHEDULER)[pid]) {
#ifdef POK_NEEDS_SCHED_RMS
  case POK_SCHED_RMS:
    pok_partitions[pid].sched_func = &pok_sched_part_rms;
    break;
#endif
#ifdef POK_NEEDS_SCHED_STATIC
  case POK_SCHED_STATIC:
    pok_partitions[pid].sched_func = &pok_sched_part_static;
    break;
#endif // POK_NEEDS_SCHED_STATIC

    /*
     * Default scheduling algorithm is Round Robin.
     * Yes, it sucks
     */
  default:
    pok_partitions[pid].sched_func = &pok_sched_part_rr;
    break;
  }
#else
  pok_partitions[pid].sched_func = &pok_sched_part_rr;
#endif
}

/**
 * \brief Reinitialize a partition from scratch
 *
 * This service is only used when we have to retrieve
 * and handle errors.
 */

void pok_partition_reinit(const uint8_t pid) {
  uint32_t tmp;
  /*
   * FIXME: reset queueing/sampling ports too
   */
  pok_partition_setup_scheduler(pid);

  pok_partitions[pid].thread_index = 0;
  CURRENT_THREAD(pok_partitions[pid]) = pok_partitions[pid].thread_index_low;
  PREV_THREAD(pok_partitions[pid]) =
      IDLE_THREAD; // breaks the rule of prev_thread not being idle, but it's
                   // just for init

  pok_partitions[pid].thread_error = 0;
  pok_partitions[pid].error_status.failed_thread = 0;
  pok_partitions[pid].error_status.failed_addr = 0;
  pok_partitions[pid].error_status.error_kind = POK_ERROR_KIND_INVALID;
  pok_partitions[pid].error_status.msg_size = 0;

  pok_loader_load_partition(
      pid, pok_partitions[pid].base_addr - pok_partitions[pid].base_vaddr,
      &tmp);

  pok_partitions[pid].thread_main_entry = tmp;

  pok_partition_setup_main_thread(pid);
}

/**
 * Setup the main thread of partition with number \a pid
 */
void pok_partition_setup_main_thread(const uint8_t pid) {
  uint32_t main_thread;
  pok_thread_attr_t attr;
#ifdef POK_NEEDS_DEBUG
  printf("Setting up main thread for partition %d\n", pid);
  printf("  partition base_addr=0x%x\n", pok_partitions[pid].base_addr);
  printf("  thread_main_entry=0x%x\n", pok_partitions[pid].thread_main_entry);
#endif

  attr.entry = (uint32_t *)pok_partitions[pid].thread_main_entry;
  attr.priority = 1;
  attr.deadline = 0;
  attr.period = INFINITE_TIME_VALUE;
  attr.time_capacity = INFINITE_TIME_VALUE;
  attr.processor_affinity = 0;

  pok_ret_t ret = pok_partition_thread_create(&main_thread, &attr, pid);
#ifdef POK_NEEDS_DEBUG
  printf("Created main thread %d for partition %d (ret=%d)\n", main_thread, pid,
         ret);
#else
  (void)ret; /* Suppress unused variable warning when debug is disabled */
#endif
  pok_partitions[pid].thread_main = main_thread;
}

/**
 * \brief Initialize all partitions.
 *
 * It initializes everything, load the program, set thread
 * and lockobjects bounds.
 */
pok_ret_t pok_partition_init() {
  uint8_t i;
  uint32_t threads_index = 0;
#ifdef POK_NEEDS_DEBUG
  printf("Starting pok_partition_init()\n");
#endif

  const uint32_t partition_size[POK_CONFIG_NB_PARTITIONS] =
      POK_CONFIG_PARTITIONS_SIZE;
#ifdef POK_CONFIG_PARTITIONS_LOADADDR
  const uint32_t program_loadaddr[POK_CONFIG_NB_PARTITIONS] =
      POK_CONFIG_PROGRAM_LOADADDR;
#ifdef POK_NEEDS_DEBUG
  printf("Fixed load addresses configured: ");
  for (int j = 0; j < POK_CONFIG_NB_PARTITIONS; j++) {
    printf("part%d=0x%x ", j, program_loadaddr[j]);
  }
  printf("\n");
#endif
#endif
#ifdef POK_NEEDS_LOCKOBJECTS
  uint8_t lockobj_index = 0;
#endif

  for (i = 0; i < POK_CONFIG_NB_PARTITIONS; i++) {
#ifdef POK_NEEDS_DEBUG
    printf("Initializing partition %d\n", i);
#endif
    uint32_t size = partition_size[i];
#ifdef POK_NEEDS_DEBUG
    printf("Allocating partition %d: size=0x%x\n", i, size);
#endif
#ifndef POK_CONFIG_PARTITIONS_LOADADDR
    uint32_t base_addr = (uint32_t)pok_bsp_mem_alloc(partition_size[i]);
#else
    uint32_t base_addr = program_loadaddr[i];
#endif
#ifdef POK_NEEDS_DEBUG
    printf("Partition %d: base_addr=0x%x\n", i, base_addr);
#endif
    uint32_t program_entry;
    uint32_t base_vaddr = pok_space_base_vaddr(base_addr);

#ifdef POK_ARCH_ARM
    /* Declare variables for W^X code region management */
    uint32_t code_addr;
    uint32_t code_size;
    pok_ret_t result;
#endif

    pok_partitions[i].base_addr = base_addr;
    pok_partitions[i].size = size;
    pok_partitions[i].sched = POK_SCHED_RR;

#ifdef POK_NEEDS_DEBUG
    printf("Setting partition %d: base_addr=0x%x, size=0x%x\n", i, base_addr,
           size);
#endif

#ifdef POK_NEEDS_COVERAGE_INFOS
#include <libc.h>
    printf("[XCOV] Partition %d loaded at addr virt=|%x|, phys=|%x|\n", i,
           base_vaddr, base_addr);
#endif

    pok_partition_setup_scheduler(i);

#ifdef POK_NEEDS_DEBUG
    printf("About to create space for partition %d: base=0x%x, size=0x%x\n", i,
           base_addr, size);
#endif

#ifdef POK_ARCH_ARM
    /* For W^X security, only create MPU data region for the data portion
     * Code region will be created separately with RX permissions
     * Data region: base + code_size, size data_size (covers .data, .bss, stack)
     *
     * IMPORTANT: We pass the data region address to pok_create_space, which
     * will set spaces[].phys_base to the data region address. This is incorrect
     * for code region validation, so we manually fix it after.
     */
    uint32_t data_region_addr = base_addr + POK_PARTITION_DATA_OFFSET;
    uint32_t data_region_size = POK_PARTITION_DATA_SIZE;
    pok_create_space(i, data_region_addr, data_region_size);

    /* Fix phys_base to point to actual partition base for code region
     * validation */
    pok_space_set_bounds(i, base_addr, size);

    /* W^X Security: Disable MPU during ELF loading
     * The ELF loader needs to write to both code and data regions.
     * Rather than manage complex RW→RX transitions, we temporarily disable
     * the MPU, load the ELF, then create proper RX code regions and re-enable.
     */
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("Disabling MPU for ELF load\n", 28);
#endif
    extern pok_ret_t pok_mpu_disable(void);
    pok_mpu_disable();
#else
    /* Other architectures: create space for entire partition */
    pok_create_space(i, base_addr, size);
#endif

#ifdef POK_NEEDS_DEBUG
    printf("Space created for partition %d\n", i);
#endif

    pok_partitions[i].base_vaddr = base_vaddr;
    /* Set the memory space and so on */

    pok_partitions[i].thread_index_low = threads_index;
    pok_partitions[i].nthreads =
        ((uint32_t[])POK_CONFIG_PARTITIONS_NTHREADS)[i];

#ifdef POK_NEEDS_DEBUG
    printf(
        "Partition %d: nthreads=%u (expected: partition 0=3, partition 1=2)\n",
        i, pok_partitions[i].nthreads);
#endif

    if (pok_partitions[i].nthreads < 1) {
#ifdef POK_NEEDS_DEBUG
      printf("ERROR: Partition %d has nthreads=%u < 1\n", i,
             pok_partitions[i].nthreads);
#endif
      pok_partition_error(i, POK_ERROR_KIND_PARTITION_CONFIGURATION);
    }

#ifdef POK_CONFIG_PARTITIONS_SCHEDULER
    pok_partitions[i].sched =
        ((pok_sched_t[])POK_CONFIG_PARTITIONS_SCHEDULER)[i];
#endif

    pok_partitions[i].thread_index_high =
        pok_partitions[i].thread_index_low +
        ((uint32_t[])POK_CONFIG_PARTITIONS_NTHREADS)[i];
    pok_partitions[i].activation = 0;
    pok_partitions[i].period = 0;
    pok_partitions[i].thread_index = 0;
    pok_partitions[i].thread_main = 0;
    pok_partitions[i].thread_main_proc = get_default_proc_real_id(i);
    for (int j = 0; j < POK_CONFIG_NB_PROCESSORS; j++) {
      pok_partitions[i].current_thread[j] = POK_CONFIG_NB_THREADS - 2 - j;
      pok_partitions[i].prev_thread[j] =
          POK_CONFIG_NB_THREADS - 2 - j; // breaks the rule of prev_thread not
                                         // being idle, but it's just for init
    }

    threads_index = threads_index + pok_partitions[i].nthreads;
    /* Initialize the threading stuff */

    pok_partitions[i].mode = POK_PARTITION_MODE_INIT_WARM;

    for (uint8_t i = 0; i < POK_CONFIG_NB_PROCESSORS; i++) {
      current_threads[i] = KERNEL_THREAD;
    }

#ifdef POK_NEEDS_LOCKOBJECTS
    pok_partitions[i].lockobj_index_low = lockobj_index;
    pok_partitions[i].lockobj_index_high =
        lockobj_index + ((uint8_t[])POK_CONFIG_PARTITIONS_NLOCKOBJECTS)[i];
    pok_partitions[i].nlockobjs =
        ((uint8_t[])POK_CONFIG_PARTITIONS_NLOCKOBJECTS)[i];
    lockobj_index = lockobj_index + pok_partitions[i].nlockobjs;
    /* Initialize mutexes stuff */
#endif

    pok_partitions[i].thread_error = 0;
    pok_partitions[i].error_status.failed_thread = 0;
    pok_partitions[i].error_status.failed_addr = 0;
    pok_partitions[i].error_status.error_kind = POK_ERROR_KIND_INVALID;
    pok_partitions[i].error_status.msg_size = 0;

#ifdef POK_NEEDS_DEBUG
    printf("About to call pok_loader_load_partition for partition %d\n", i);
#endif

#ifdef POK_ARCH_ARM
    /* ARM: Partitions are linked at absolute addresses (0x20010000,
     * 0x20014000), not at 0. No offset needed since ELF already contains
     * correct addresses. */
    pok_loader_load_partition(i, 0, &program_entry);
#ifdef POK_NEEDS_DEBUG
    printf("PART_INIT: partition=%d base_addr=0x%x program_entry=0x%x\n", i,
           base_addr, program_entry);
#endif
#else
    /* Other architectures: offset is difference between physical and virtual */
    pok_loader_load_partition(i, base_addr - base_vaddr, &program_entry);
#endif
    /*
     * Load the partition in its address space
     */
    pok_partitions[i].thread_main_entry = program_entry;

#ifdef POK_ARCH_ARM
    /* W^X Security: Create RX code region and re-enable MPU
     * The partition linker script separates:
     *   - Code region: .text and .rodata at base address (RX)
     *   - Data region: .data, .bss, stack at base + code_size (RW)
     *
     * Now that ELF is loaded, create code region with RX permissions.
     * Data region was already created with RW+XN permissions.
     * This enforces W^X: code is executable but not writable,
     * data/stack is writable but not executable.
     */
    code_addr = base_vaddr;
    code_size = POK_PARTITION_CODE_SIZE;

    extern pok_ret_t pok_create_code_region(
        uint8_t partition_id, uint32_t code_addr, uint32_t code_size);
    result = pok_create_code_region(i, code_addr, code_size);
    if (result != POK_ERRNO_OK) {
      pok_cons_write("ERROR: Code region creation failed\n", 36);
      return POK_ERRNO_EFAULT;
    }

#ifdef POK_NEEDS_DEBUG
    pok_cons_write("Re-enabling MPU with W^X protection\n", 37);
#endif
    extern pok_ret_t pok_mpu_enable(void);
    pok_mpu_enable();

    pok_cons_write(
        "W^X enforced - partition has separate RX code and RW data regions\n",
        67);
#endif

    pok_partitions[i].lock_level = 0;
    pok_partitions[i].start_condition = NORMAL_START;

#ifdef POK_NEEDS_INSTRUMENTATION
    pok_instrumentation_partition_archi(i);
#endif

#ifdef POK_NEEDS_DEBUG
    printf("About to setup main thread for partition %d\n", i);
#endif
    pok_partition_setup_main_thread(i);
#ifdef POK_NEEDS_DEBUG
    printf("Finished setting up main thread for partition %d\n", i);
#endif
  }

  return POK_ERRNO_OK;
}

/**
 * Change the current mode of the partition. Possible mode
 * are describe in core/partition.h. Returns
 * POK_ERRNO_PARTITION_MODE when requested mode is invalid.
 * Else, returns POK_ERRNO_OK
 */
pok_ret_t pok_partition_set_mode(const uint8_t pid,
                                 const pok_partition_mode_t mode) {
  switch (mode) {
  case POK_PARTITION_MODE_NORMAL:
#ifdef POK_NEEDS_DEBUG
    printf("Transitioning partition %d to NORMAL mode\n", pid);
#endif
    /*
     * We first check that a partition that wants to go
     * to the NORMAL mode is currently in the INIT mode
     */

    if (pok_partitions[pid].mode == POK_PARTITION_MODE_IDLE) {
      return POK_ERRNO_PARTITION_MODE;
    }

    /* Allow kernel thread to transition partitions during boot */
    if (POK_SCHED_CURRENT_THREAD != KERNEL_THREAD &&
        POK_SCHED_CURRENT_THREAD != pok_partitions[pid].thread_main) {
      return POK_ERRNO_PARTITION_MODE;
    }

    pok_partitions[pid].mode = mode; /* Here, we change the mode */

    pok_thread_t *thread;
    unsigned int i;
#ifdef POK_NEEDS_DEBUG
    printf("Processing %d threads for partition %d (indices %d-%d)\n",
           pok_partitions[pid].nthreads, pid,
           pok_partitions[pid].thread_index_low,
           pok_partitions[pid].thread_index_high - 1);
#endif
    for (i = 0; i < pok_partitions[pid].nthreads; i++) {
      thread = &(pok_threads[pok_partitions[pid].thread_index_low + i]);
#ifdef POK_NEEDS_DEBUG
      printf("Thread %d: state=%d, period=%lld, wakeup_time=%llu",
             pok_partitions[pid].thread_index_low + i, thread->state,
             (long long)thread->period, thread->wakeup_time);
      if (pok_partitions[pid].thread_index_low + i ==
          pok_partitions[pid].thread_main) {
        printf(" [MAIN THREAD]");
      }
      printf("\n");
#endif
      if ((long long)thread->period == INFINITE_TIME_VALUE) {
#ifdef POK_NEEDS_DEBUG
        printf("  -> Thread %d has INFINITE period\n",
               pok_partitions[pid].thread_index_low + i);
#endif
        if (thread->state ==
            POK_STATE_DELAYED_START) { // delayed start, the delay is in the
                                       // wakeup time
#ifdef POK_NEEDS_DEBUG
          printf("  -> Transitioning infinite period thread %d from "
                 "DELAYED_START\n",
                 pok_partitions[pid].thread_index_low + i);
#endif
          if (!thread->wakeup_time) {
            thread->state = POK_STATE_RUNNABLE;
            thread->wakeup_time = POK_GETTICK();
            if (thread->time_capacity > 0)
              thread->end_time = thread->wakeup_time + thread->time_capacity;
#ifdef POK_NEEDS_DEBUG
            printf("  -> Infinite period thread %d set to RUNNABLE\n",
                   pok_partitions[pid].thread_index_low + i);
#endif
          } else {
#ifdef POK_NEEDS_DEBUG
            printf(
                "  -> Infinite period thread %d has delayed wakeup_time=%lld\n",
                pok_partitions[pid].thread_index_low + i,
                (long long)thread->wakeup_time);
#endif
            thread->state = POK_STATE_WAITING;
          }
        } else {
#ifdef POK_NEEDS_DEBUG
          printf("  -> Thread %d not in DELAYED_START (state=%d)\n",
                 pok_partitions[pid].thread_index_low + i, thread->state);
#endif
        }
      } else {
#ifdef POK_NEEDS_DEBUG
        printf("  -> Thread %d does not have INFINITE period (period=%lld)\n",
               pok_partitions[pid].thread_index_low + i,
               (long long)thread->period);
#endif
        if (thread->state ==
            POK_STATE_DELAYED_START) { // delayed start, the delay is in the
                                       // wakeup time
#ifdef POK_NEEDS_DEBUG
          printf("  -> Transitioning periodic thread %d from DELAYED_START\n",
                 pok_partitions[pid].thread_index_low + i);
#endif
          if (!thread->wakeup_time) {
            thread->state = POK_STATE_RUNNABLE;
            thread->wakeup_time = POK_GETTICK();
            if (thread->time_capacity > 0)
              thread->end_time = thread->wakeup_time + thread->time_capacity;
#ifdef POK_NEEDS_DEBUG
            printf("  -> Periodic thread %d set to RUNNABLE\n",
                   pok_partitions[pid].thread_index_low + i);
#endif
          } else {
            thread->next_activation = thread->wakeup_time +
                                      POK_CONFIG_SCHEDULING_MAJOR_FRAME +
                                      POK_CURRENT_PARTITION.activation;
            thread->end_time = thread->next_activation + thread->time_capacity;
            thread->state = POK_STATE_WAIT_NEXT_ACTIVATION;
#ifdef POK_NEEDS_DEBUG
            printf("  -> Periodic thread %d set to WAIT_NEXT_ACTIVATION\n",
                   pok_partitions[pid].thread_index_low + i);
#endif
          }
        }
      }
    }
    pok_sched_stop_thread(pok_partitions[pid].thread_main);
    /* We stop the thread that call this change. All the time,
     * the thread that init this request is the init thread.
     * When it calls this function, the partition is ready and
     * this thread does not need no longer to be executed
     */

    pok_global_sched();
    /*
     * Reschedule, baby, reschedule !
     * In fact, the init thread is stopped, we need to execute
     * the other threads.
     */
    break;

  case POK_PARTITION_MODE_STOPPED:

    /*
     * Only the error thread can stop the partition
     */
    if ((POK_CURRENT_PARTITION.thread_error == 0) ||
        (POK_SCHED_CURRENT_THREAD != POK_CURRENT_PARTITION.thread_error)) {
      return POK_ERRNO_PARTITION_MODE;
    }

    pok_partitions[pid].mode = mode; /* Here, we change the mode */
    pok_global_sched();
    break;

  case POK_PARTITION_MODE_INIT_WARM:
  case POK_PARTITION_MODE_INIT_COLD:
    if (pok_partitions[pid].mode == POK_PARTITION_MODE_INIT_COLD &&
        mode == POK_PARTITION_MODE_INIT_WARM) {
      return POK_ERRNO_PARTITION_MODE;
    }

    /*
     * Check that only the error thread can restart the partition
     */
    if ((POK_CURRENT_PARTITION.thread_error == 0) ||
        (POK_SCHED_CURRENT_THREAD != POK_CURRENT_PARTITION.thread_error)) {
      return POK_ERRNO_PARTITION_MODE;
    }

    /*
     * The partition fallback in the INIT_WARM mode when it
     * was in the NORMAL mode. So, we check the previous mode
     */

    pok_partitions[pid].mode = mode; /* Here, we change the mode */

    pok_partition_reinit(pid);

    pok_global_sched();

    break;

  default:
    return POK_ERRNO_PARTITION_MODE;
  }
  return POK_ERRNO_OK;
}

/**
 * Change the mode of the current partition (the partition being executed)
 */
pok_ret_t pok_partition_set_mode_current(const pok_partition_mode_t mode) {
  if ((POK_SCHED_CURRENT_THREAD != POK_CURRENT_PARTITION.thread_main) &&
      (POK_SCHED_CURRENT_THREAD != POK_CURRENT_PARTITION.thread_error)) {
    return POK_ERRNO_THREAD;
  }

  /*
   * Here, we check which thread call this function.
   * In fact, only two threads can change the partition mode : the init thread
   * and the error thread. If ANY other thread try to change the partition
   * mode, this is an error !
   */
  return (pok_partition_set_mode(POK_SCHED_CURRENT_PARTITION, mode));
}

/**
 * Get partition information. Used for ARINC GET_PARTITION_STATUS function.
 */
pok_ret_t pok_current_partition_get_id(uint8_t *id) {
  *id = POK_SCHED_CURRENT_PARTITION;
  return POK_ERRNO_OK;
}

pok_ret_t pok_current_partition_get_period(uint64_t *period) {
  *period = POK_CURRENT_PARTITION.period;
  return POK_ERRNO_OK;
}

pok_ret_t pok_current_partition_get_duration(uint64_t *duration) {
  *duration = pok_sched_slots[POK_SCHED_CURRENT_PARTITION];
  return POK_ERRNO_OK;
}

pok_ret_t
pok_current_partition_get_operating_mode(pok_partition_mode_t *op_mode) {
  *op_mode = POK_CURRENT_PARTITION.mode;
  return POK_ERRNO_OK;
}

pok_ret_t pok_current_partition_get_lock_level(uint32_t *lock_level) {
  *lock_level = POK_CURRENT_PARTITION.lock_level;
  return POK_ERRNO_OK;
}

pok_ret_t pok_current_partition_get_start_condition(
    pok_start_condition_t *start_condition) {
  *start_condition = POK_CURRENT_PARTITION.start_condition;
  return POK_ERRNO_OK;
}

/**
 * Stop a thread inside a partition.
 * The \a tid argument is relative to the partition, meaning
 * that it corresponds to a number which bounds are
 * 0 .. number of tasks inside the partition.
 */
pok_ret_t pok_partition_stop_thread(const uint32_t tid) {
  uint32_t id;
  if (POK_SCHED_CURRENT_THREAD != POK_CURRENT_PARTITION.thread_error) {
    return POK_ERRNO_THREAD;
  }

  id = tid + POK_CURRENT_PARTITION.thread_index_low;
  if (POK_CURRENT_PARTITION.thread_index_low > id ||
      POK_CURRENT_PARTITION.thread_index_high < id) {
    return POK_ERRNO_THREADATTR;
  }

  /*
   * We check which thread try to call this function. Only the error handling
   * thread can stop other threads.
   */

  pok_sched_stop_thread(id);
  pok_global_sched();
  return (POK_ERRNO_OK);
}

/**
 * The \a tid argument is relative to partition thread index
 */
pok_ret_t pok_partition_restart_thread(const uint32_t tid) {
  uint32_t id;
  if (POK_SCHED_CURRENT_THREAD != POK_CURRENT_PARTITION.thread_error) {
    return POK_ERRNO_THREAD;
  }

  id = tid + POK_CURRENT_PARTITION.thread_index_low;
  if (POK_CURRENT_PARTITION.thread_index_low > id ||
      POK_CURRENT_PARTITION.thread_index_high < id) {
    return POK_ERRNO_THREADATTR;
  }

  /*
   * We check which thread try to call this function. Only the error handling
   * thread can stop other threads.
   */

  pok_thread_restart(id);
  pok_global_sched();
  return (POK_ERRNO_OK);
}
