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
 **\\file   sched.c
 **\\brief  Function for partitions and kernel scheduling
 **\\author Julien Delange
 */

/* ARM architecture now supported */

#include <arch.h>
#include <assert.h>
#include <types.h>

#include <bsp.h>
#include <core/multiprocessing.h>
#include <core/sched.h>
#include <core/thread.h>
#include <core/time.h>

/* ARM single-core thread definitions */
#ifndef KERNEL_THREAD
#define KERNEL_THREAD (POK_CONFIG_NB_THREADS - 1)
#endif
#ifndef IDLE_THREAD
#define IDLE_THREAD (POK_CONFIG_NB_THREADS - 2 - (uint32_t)(pok_get_proc_id()))
#endif

#include <arch.h>
#ifdef POK_ARCH_X86
#include <arch/x86/ipi.h>
#endif
#ifdef POK_ARCH_ARM
#include <arch/arm/ipi.h>
#include <arch/arm/rendezvous.h>
#endif
#include <core/partition.h>

#ifdef POK_NEEDS_MIDDLEWARE
#include <middleware/port.h>
#endif

#include <dependencies.h>

#include <core/debug.h>
#include <core/error.h>
#include <core/instrumentation.h>

#ifdef POK_NEEDS_LOCKOBJECTS
#include <core/lockobj.h>

extern pok_lockobj_t pok_partitions_lockobjs[];
#endif

extern pok_thread_t pok_threads[];

#if defined(POK_NEEDS_DEBUG)
static const char *state_names[] = {
    "stopped",      "runnable", "waiting", "lock", "waiting next activation",
    "delayed start"};
#endif

extern pok_partition_t pok_partitions[];

/**
 * \brief The variable that contains the value of partition currently being
 * executed
 */
uint8_t pok_current_partition;
rendezvous fence, barr;

void pok_sched_partition_switch();

#if defined(POK_NEEDS_PORTS_SAMPLING) || defined(POK_NEEDS_PORTS_QUEUEING)
extern void pok_port_flushall(void);
extern void pok_port_flush_partition(uint8_t);
#endif

/* Scheduling slots and allocation arrays - values from deployment.h */
uint64_t pok_sched_slots[POK_CONFIG_SCHEDULING_NBSLOTS] =
    POK_CONFIG_SCHEDULING_SLOTS;
uint8_t pok_sched_slots_allocation[POK_CONFIG_SCHEDULING_NBSLOTS] =
    POK_CONFIG_SCHEDULING_SLOTS_ALLOCATION;

uint64_t pok_sched_next_deadline;
uint64_t pok_sched_next_major_frame;
uint64_t pok_sched_next_flush; // variable used to handle user defined
                               // flushing period, i.e. distinct from
                               // MAF and from partition slot
                               // boundaries

/* CRITICAL: Use uint32_t instead of uint8_t to avoid corruption from adjacent
 * writes */
volatile uint32_t pok_sched_current_slot =
    0; /* Which slot are we executing at this time ?*/

extern int spinlocks[POK_CONFIG_NB_PROCESSORS];

void pok_sched_thread_switch(void);

/**
 *\\brief Init scheduling service
 */

/* Flag to prevent scheduling before initialization is complete */
static uint8_t pok_sched_initialized = 0;

void pok_sched_init(void) {
  /*
   * We check that the total time of time frame
   * corresponds to the sum of each slot
   */
  uint64_t total_time;
  uint8_t slot;
  fence = barr = 0;

  total_time = 0;

  for (slot = 0; slot < POK_CONFIG_SCHEDULING_NBSLOTS; slot++) {
    total_time = total_time + pok_sched_slots[slot];
  }

  if (total_time != POK_CONFIG_SCHEDULING_MAJOR_FRAME) {
#ifdef POK_NEEDS_DEBUG
    printf("Major frame is not compliant with all time slots\n");
#endif
    pok_kernel_error(POK_ERROR_KIND_KERNEL_CONFIG);
  }

  pok_sched_current_slot = 0;
  pok_sched_next_major_frame = POK_CONFIG_SCHEDULING_MAJOR_FRAME;
  pok_sched_next_deadline = pok_sched_slots[0];
  pok_sched_next_flush = 0;

#ifdef POK_NEEDS_DEBUG
  pok_cons_write("SCHED_INIT called - resetting slot to 0\n", 41);

  /* Force evaluation to prevent optimization */
  if (pok_sched_slots[0] > 0) {
    pok_cons_write("SCHED_INIT: slot0 > 0\n", 22);
  }
  if (pok_sched_next_deadline > 0) {
    pok_cons_write("SCHED_INIT: deadline > 0\n", 25);
  }

  /* DEBUG: Print ALL thread states at initialization */
  extern pok_thread_t pok_threads[POK_CONFIG_NB_THREADS];

  pok_cons_write("\n=== ALL THREADS AT BOOT ===\n", 29);
  char buf[80];
  for (uint8_t tid = 0; tid < POK_CONFIG_NB_THREADS; tid++) {
    /* Skip if entry is NULL (thread not initialized) */
    if (pok_threads[tid].entry == 0) {
      continue;
    }

    pok_cons_write("Thread ", 7);
    buf[0] = '0' + (tid / 10);
    buf[1] = '0' + (tid % 10);
    pok_cons_write(buf, 2);

    pok_cons_write(": part=", 7);
    buf[0] = '0' + pok_threads[tid].partition;
    pok_cons_write(buf, 1);

    pok_cons_write(" state=", 7);
    buf[0] = '0' + pok_threads[tid].state;
    pok_cons_write(buf, 1);

    pok_cons_write(" priority=", 10);
    buf[0] = '0' + pok_threads[tid].priority;
    pok_cons_write(buf, 1);

    pok_cons_write(" entry=0x", 9);
    uint32_t entry = (uint32_t)pok_threads[tid].entry;
    for (int i = 7; i >= 0; i--) {
      uint8_t nibble = (entry >> (i * 4)) & 0xF;
      buf[7 - i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    pok_cons_write(buf, 8);
    pok_cons_write("\n", 1);
  }
  pok_cons_write("===========================\n\n", 30);
#endif
  pok_current_partition = pok_sched_slots_allocation[0];

  /* ARM: Enable initial partition's MPU regions before scheduling starts */
#ifdef POK_ARCH_ARM
  pok_space_switch(POK_CONFIG_NB_PARTITIONS, /* invalid old partition */
                   pok_current_partition);

#ifdef POK_NEEDS_DEBUG
  /* Dump all MPU regions for debugging */
  pok_cons_write("\n=== All MPU Regions ===\n", 24);

/* Access MPU registers directly */
#define MPU_BASE 0xE000ED90
#define MPU_TYPE_REG (*((volatile uint32_t *)(MPU_BASE + 0x00)))
#define MPU_CTRL_REG (*((volatile uint32_t *)(MPU_BASE + 0x04)))
#define MPU_RNR_REG (*((volatile uint32_t *)(MPU_BASE + 0x08)))
#define MPU_RBAR_REG (*((volatile uint32_t *)(MPU_BASE + 0x0C)))
#define MPU_RASR_REG (*((volatile uint32_t *)(MPU_BASE + 0x10)))

  uint32_t mpu_ctrl = MPU_CTRL_REG;
  pok_cons_write("MPU_CTRL=0x", 11);
  char ctrl_buf[9];
  for (int j = 7; j >= 0; j--) {
    ctrl_buf[j] = "0123456789ABCDEF"[(mpu_ctrl >> ((7 - j) * 4)) & 0xF];
  }
  pok_cons_write(ctrl_buf, 8);
  pok_cons_write(mpu_ctrl & 1 ? " [ENABLED]\n" : " [DISABLED]\n", 12);

  uint8_t num_regions = (MPU_TYPE_REG >> 8) & 0xFF;

  for (uint8_t i = 0; i < num_regions; i++) {
    MPU_RNR_REG = i;
    uint32_t rbar = MPU_RBAR_REG;
    uint32_t rasr = MPU_RASR_REG;

    if (rasr & 1) { /* If enabled */
      char buf[80];
      pok_cons_write("Region ", 7);
      buf[0] = '0' + i;
      buf[1] = ':';
      buf[2] = ' ';
      pok_cons_write(buf, 3);

      pok_cons_write("RBAR=0x", 7);
      for (int j = 7; j >= 0; j--) {
        buf[j] = "0123456789ABCDEF"[(rbar >> ((7 - j) * 4)) & 0xF];
      }
      pok_cons_write(buf, 8);

      pok_cons_write(" RASR=0x", 8);
      for (int j = 7; j >= 0; j--) {
        buf[j] = "0123456789ABCDEF"[(rasr >> ((7 - j) * 4)) & 0xF];
      }
      pok_cons_write(buf, 8);

      pok_cons_write(rasr & (1 << 28) ? " [XN]" : " [EXEC]", 7);
      pok_cons_write("\n", 1);
    }
  }
  pok_cons_write("======================\n\n", 24);
#endif
#endif

  /* Enable scheduling after initialization */
  pok_sched_initialized = 1;
}

uint8_t pok_sched_get_priority_min(const pok_sched_t sched_type) {
  (void)sched_type;
  /* At this time, we only support one scheduler */
  return 0;
}

uint8_t pok_sched_get_priority_max(const pok_sched_t sched_type) {
  (void)sched_type;
  /* At this time, we only support one scheduler */
  return 255;
}

uint8_t pok_elect_partition() {
  static uint32_t call_count = 0;
  call_count++;
  if (call_count <= 5) {
    pok_cons_write("ELECT_CALLED: call#", 19);
    char buf[2];
    buf[0] = '0' + call_count;
    pok_cons_write(buf, 1);
    pok_cons_write("\n", 1);
  }

  uint8_t next_partition = POK_SCHED_CURRENT_PARTITION;
#if POK_CONFIG_NB_PARTITIONS > 1
  uint64_t now = POK_GETTICK();

  /* Debug: Show deadline comparison every 100ms (UNCONDITIONAL) */
  static uint64_t last_debug = 0;
  if (now - last_debug >= 100000000ULL) { /* 100ms = 100,000,000 ns */
    pok_cons_write("ELECT: now=", 11);
    char buf[20];
    int len = 0;
    uint64_t val = now;
    do {
      buf[len++] = '0' + (val % 10);
      val /= 10;
    } while (val && len < 19);
    for (int i = 0; i < len / 2; i++) {
      char t = buf[i];
      buf[i] = buf[len - 1 - i];
      buf[len - 1 - i] = t;
    }
    pok_cons_write(buf, len);

    pok_cons_write(" deadline=", 10);
    len = 0;
    val = pok_sched_next_deadline;
    do {
      buf[len++] = '0' + (val % 10);
      val /= 10;
    } while (val && len < 19);
    for (int i = 0; i < len / 2; i++) {
      char t = buf[i];
      buf[i] = buf[len - 1 - i];
      buf[len - 1 - i] = t;
    }
    pok_cons_write(buf, len);

    pok_cons_write(" slot=", 6);
    buf[0] = '0' + pok_sched_current_slot;
    pok_cons_write(buf, 1);

    pok_cons_write(" part=", 6);
    buf[0] = '0' + pok_current_partition;
    pok_cons_write(buf, 1);
    pok_cons_write("\n", 1);

    last_debug = now;
  }

  /* Check for port flush first - this should happen at major frame boundaries
   */
#if defined(POK_NEEDS_PORTS_SAMPLING) || defined(POK_NEEDS_PORTS_QUEUEING)
#if defined(POK_FLUSH_PERIOD)
  // Flush periodically all partition ports
  // nb : Flush periodicity is a multiple of POK time base.
  if (pok_sched_next_flush <= now) {
    pok_sched_next_flush += POK_FLUSH_PERIOD;
    pok_port_flushall();
  }
#elif defined(POK_NEEDS_FLUSH_ON_WINDOWS)
  // Flush only the ports of the partition that just finished its slot
  if ((pok_sched_next_deadline <= now)) {
    pok_port_flush_partition(pok_current_partition);
  }
#else  // activate default flushing policy at each Major Frame beginning
  static int flush_debug = 0;
  if (flush_debug < 5) {
    printf("[FLUSH_DEBUG] now=%llu next_major=%llu major_frame=%llu\n", now,
           pok_sched_next_major_frame,
           (uint64_t)POK_CONFIG_SCHEDULING_MAJOR_FRAME);
    flush_debug++;
  }
  if (pok_sched_next_major_frame <= now) {
    printf("[FLUSH] Major frame flush at now=%llu next_major=%llu\n", now,
           pok_sched_next_major_frame);
    pok_sched_next_major_frame =
        pok_sched_next_major_frame + POK_CONFIG_SCHEDULING_MAJOR_FRAME;
    pok_port_flushall();
  }
#endif /* defined POK_FLUSH_PERIOD || POK_NEEDS_FLUSH_ON_WINDOWS */
#endif /* defined (POK_NEEDS_PORTS....) */

  if (pok_sched_next_deadline <= now) {
    /* Here, we change the partition */

    /* CRITICAL: Increment slot counter */
    uint32_t old_slot = pok_sched_current_slot;
    uint32_t new_slot_before_mod = old_slot + 1;
    uint32_t new_slot = new_slot_before_mod % POK_CONFIG_SCHEDULING_NBSLOTS;

    pok_sched_current_slot = new_slot;

    /* CRITICAL: Memory barrier to ensure slot write completes */
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");

    pok_sched_next_deadline =
        pok_sched_next_deadline + pok_sched_slots[pok_sched_current_slot];

    next_partition = pok_sched_slots_allocation[pok_sched_current_slot];
  }
#endif /* POK_CONFIG_NB_PARTITIONS > 1 */

  return next_partition;
}

uint32_t pok_elect_thread(uint8_t new_partition_id) {
  uint64_t now = POK_GETTICK();
  pok_partition_t *new_partition = &(pok_partitions[new_partition_id]);

  /*
   * We unlock all WAITING threads if the waiting time is passed
   */
  uint8_t i; /* i is used to browse the partition. We support
              * only 255 partitions are max, so, we use an uin8_t
              * type
              */
  pok_thread_t *thread;
  for (i = 0; i < new_partition->nthreads; i++) {
    thread = &(pok_threads[new_partition->thread_index_low + i]);

    if (thread->processor_affinity == pok_get_proc_id()) {

#if defined(POK_NEEDS_LOCKOBJECTS) || defined(POK_NEEDS_PORTS_QUEUEING) ||     \
    defined(POK_NEEDS_PORTS_SAMPLING)
      if (thread->state == POK_STATE_WAITING) {
        if (thread->wakeup_time <= now) {
          thread->state = POK_STATE_RUNNABLE;
        }
      }
#endif

      if ((thread->state == POK_STATE_WAIT_NEXT_ACTIVATION) &&
          (thread->next_activation <= now)) {
        assert(thread->time_capacity);
        thread->state = POK_STATE_RUNNABLE;
        thread->remaining_time_capacity = thread->time_capacity;
#ifdef POK_NEEDS_DEBUG
        if (i == 1 || i == 3) { // Only debug threads 1 and 3 to reduce spam
          printf(
              "[WAKE] Thread %u: now=%llu, next_act=%llu, period=%lld, "
              "new_next=%llu\n",
              i, (unsigned long long)now,
              (unsigned long long)thread->next_activation,
              (long long)thread->period,
              (unsigned long long)(thread->next_activation + thread->period));
        }
#endif
        thread->next_activation = thread->next_activation + thread->period;
      }
    }
  }

  /*
   * We elect the thread to be executed.
   */
  uint32_t elected;
  switch (new_partition->mode) {
  case POK_PARTITION_MODE_INIT_COLD:
  case POK_PARTITION_MODE_INIT_WARM:
    if (pok_get_proc_id() == new_partition->thread_main_proc) {
      /* Activate main thread if it's in DELAYED_START state */
      if (pok_threads[new_partition->thread_main].state ==
          POK_STATE_DELAYED_START) {
        pok_threads[new_partition->thread_main].state = POK_STATE_RUNNABLE;
      }

      /* Continue with existing logic below */

      if ((new_partition->thread_error != 0) &&
          (pok_threads[new_partition->thread_error].state !=
           POK_STATE_STOPPED)) {
        elected = new_partition->thread_error;
      } else {
        elected = new_partition->thread_main;
      }

      elected = new_partition->thread_main;
    } else
      elected = IDLE_THREAD;
    break;

  case POK_PARTITION_MODE_NORMAL:
    if ((CURRENT_THREAD(*new_partition) == new_partition->thread_error) &&
        (pok_threads[CURRENT_THREAD(*new_partition)].state ==
         POK_STATE_RUNNABLE)) {
      elected = new_partition->thread_error;
      break;
    }
    if ((POK_SCHED_CURRENT_THREAD != IDLE_THREAD) &&
        (POK_SCHED_CURRENT_THREAD != POK_CURRENT_PARTITION.thread_main) &&
        (POK_SCHED_CURRENT_THREAD != POK_CURRENT_PARTITION.thread_error)) {
      if (POK_CURRENT_THREAD.remaining_time_capacity > 0) {
        POK_CURRENT_THREAD.remaining_time_capacity =
            POK_CURRENT_THREAD.remaining_time_capacity - 1;
      } else if (POK_CURRENT_THREAD.time_capacity >
                 0) // Wait next activation only for thread
                    // with non-infinite capacity (could be
                    // infinite with value -1 <--> INFINITE_TIME_CAPACITY)
      {
        POK_CURRENT_THREAD.state = POK_STATE_WAIT_NEXT_ACTIVATION;
      }
    }
    elected = new_partition->sched_func(
        new_partition->thread_index_low, new_partition->thread_index_high,
        PREV_THREAD(*new_partition), CURRENT_THREAD(*new_partition));
#ifdef POK_NEEDS_INSTRUMENTATION
    if ((elected != IDLE_THREAD) && (elected != new_partition->thread_main)) {
      pok_instrumentation_running_task(elected);
    }
#endif

    break;

  default:
    elected = IDLE_THREAD;
    break;
  }

  // computed next thread's deadline
  if (pok_threads[POK_SCHED_CURRENT_THREAD].time_capacity > 0)
    pok_threads[elected].end_time =
        now + pok_threads[elected].remaining_time_capacity;

  return elected;
}

// Global scheduling with partition

uint8_t new_partition;

void pok_global_sched_thread(bool_t is_source_processor) {
  uint8_t elected_thread = pok_elect_thread(POK_SCHED_CURRENT_PARTITION);

  if (CURRENT_THREAD(pok_partitions[POK_SCHED_CURRENT_PARTITION]) !=
      elected_thread) {
    if (CURRENT_THREAD(pok_partitions[POK_SCHED_CURRENT_PARTITION]) !=
        IDLE_THREAD) {
      PREV_THREAD(pok_partitions[POK_SCHED_CURRENT_PARTITION]) =
          CURRENT_THREAD(pok_partitions[POK_SCHED_CURRENT_PARTITION]);
    }
    CURRENT_THREAD(pok_partitions[POK_SCHED_CURRENT_PARTITION]) =
        elected_thread;
  }
  pok_global_sched_context_switch(elected_thread, is_source_processor);
}
void pok_global_sched() {
  uint8_t elected_partition = POK_SCHED_CURRENT_PARTITION;
  elected_partition = pok_elect_partition();
  new_partition = elected_partition != POK_SCHED_CURRENT_PARTITION;
  POK_SCHED_CURRENT_PARTITION = elected_partition;

  if (multiprocessing_system) {
    start_rendezvous(&fence);
    pok_send_global_schedule_thread();
  }
  pok_global_sched_thread(TRUE);
}

void synchro_processors(const uint32_t elected_id, bool_t is_source_processor) {
  if (!is_source_processor) {
    join_rendezvous(&fence);
    join_rendezvous(&barr);
  } else {
    if (multiprocessing_system) {
      spin_wait_for_rendezvous(&fence, multiprocessing_system);
      if (new_partition) {
        pok_space_switch(POK_CURRENT_THREAD.partition,
                         pok_threads[elected_id].partition);
      }
      start_rendezvous(&barr);
      unblock_rendezvous(&fence);
      spin_wait_for_rendezvous(&barr, multiprocessing_system);
      unblock_rendezvous(&barr);
    } else if (new_partition) {
      pok_space_switch(POK_CURRENT_THREAD.partition,
                       pok_threads[elected_id].partition);
    }
  }
}

/*
 * Context-switch function to switch from one thread to another
 * Rely on architecture-dependent functionnalities (must include arch.h)
 */
void pok_global_sched_context_switch(const uint32_t elected_id,
                                     bool_t is_source_processor) {
  uint32_t *current_sp;
  uint32_t new_sp;

  if (POK_SCHED_CURRENT_THREAD == elected_id) {
    synchro_processors(elected_id, is_source_processor);

    if (!is_source_processor)
      pok_end_ipi();

  } else {

#ifdef POK_NEEDS_LOCKOBJECTS

    // Check if every spin lock is unlocked before changing context
    assert(!spinlocks[pok_get_proc_id()]);

#endif

    current_sp = &POK_CURRENT_THREAD.sp;
    new_sp = pok_threads[elected_id].sp;

    synchro_processors(elected_id, is_source_processor);

    POK_SCHED_CURRENT_THREAD = elected_id;
    if (!is_source_processor)
      pok_end_ipi();
    pok_context_switch(current_sp, new_sp);
  }
}

// Local thread sched

void pok_sched_context_switch(const uint32_t elected_id,
                              bool_t is_source_processor) {
  uint32_t *current_sp;
  uint32_t new_sp;

  /* CRITICAL FIX: On first run, POK_SCHED_CURRENT_THREAD may equal elected_id
   * but the thread has never actually executed. We MUST context switch if the
   * thread has never run (sp == 0 means no context exists yet).
   *
   * This was causing threads to never start - scheduler thought they were
   * already running when they had never been context-switched to.
   *
   * Skip context switch ONLY if: same thread AND thread has valid context (sp
   * != 0)
   */
  pok_bool_t skip_switch = (POK_SCHED_CURRENT_THREAD == elected_id) &&
                           (pok_threads[elected_id].sp != 0);

  if (skip_switch) {
    if (!is_source_processor)
      pok_end_ipi();

  } else {

#ifdef POK_NEEDS_LOCKOBJECTS
    // Check if every spin lock is unlocked before changing context
    assert(!spinlocks[pok_get_proc_id()]);
#endif

    uint8_t old_thread_id = POK_SCHED_CURRENT_THREAD;

    /* CRITICAL FIX: Kernel and idle threads use MSP, not PSP. Pass NULL to skip
     * saving. MUST use old_thread_id, not POK_CURRENT_THREAD macro, since
     * POK_SCHED_CURRENT_THREAD is updated to elected_id before context_switch
     * is called. */

    /* CRITICAL FIX: Explicitly check if we need to save the old thread.
     * Idle and kernel threads use MSP, not PSP, so they don't need saving.
     * Use explicit if-statement instead of ternary to ensure correctness. */
    if (old_thread_id == IDLE_THREAD || old_thread_id == KERNEL_THREAD) {
      current_sp = NULL; /* Don't save - idle/kernel threads use MSP */
    } else {
      current_sp = &pok_threads[old_thread_id].sp; /* Save this thread's PSP */
    }
    new_sp = pok_threads[elected_id].sp;

    POK_SCHED_CURRENT_THREAD = elected_id;
    if (!is_source_processor)
      pok_end_ipi();
    pok_context_switch(current_sp, new_sp);

    /* Note: If context switch was skipped due to pending switch,
     * POK_SCHED_CURRENT_THREAD may not match actual running thread.
     * This is okay - scheduler will be called again and try again. */
  }
}

void pok_sched_thread(bool_t is_source_processor) {
  uint8_t elected_thread = pok_elect_thread(POK_SCHED_CURRENT_PARTITION);

  if (CURRENT_THREAD(pok_partitions[POK_SCHED_CURRENT_PARTITION]) !=
      elected_thread) {
    if (CURRENT_THREAD(pok_partitions[POK_SCHED_CURRENT_PARTITION]) !=
        IDLE_THREAD) {
      PREV_THREAD(pok_partitions[POK_SCHED_CURRENT_PARTITION]) =
          CURRENT_THREAD(pok_partitions[POK_SCHED_CURRENT_PARTITION]);
    }
    CURRENT_THREAD(pok_partitions[POK_SCHED_CURRENT_PARTITION]) =
        elected_thread;
  }
  pok_sched_context_switch(elected_thread, is_source_processor);
}

// Send local thread sched

void pok_threads_schedule_one_proc(uint8_t dest) {
  if (multiprocessing_system) {
    if (dest == pok_get_proc_id())
      pok_sched_thread(TRUE);
    else
      pok_send_schedule_thread(dest);
  } else {
    assert(!dest);
    pok_sched_thread(TRUE);
  }
}

void pok_threads_schedule_every_proc() {
  if (multiprocessing_system) {
    pok_send_schedule_thread_other_processors();
  }
  pok_sched_thread(TRUE);
}

#ifdef POK_NEEDS_SCHED_RMS
uint32_t pok_sched_part_rms(const uint32_t index_low, const uint32_t index_high,
                            const uint32_t __attribute__((unused)) prev_thread,
                            const uint32_t
                            __attribute__((unused)) current_thread) {
  uint32_t res;
  uint8_t current_proc = pok_get_proc_id();
#ifdef POK_NEEDS_DEBUG
  uint32_t from;
  from = prev_thread;
#endif

  res = index_low;

  do {
    res++;
    if (res >= index_high) {
      res = index_low;
    }
  } while ((res != index_low) &&
           ((pok_threads[res].state != POK_STATE_RUNNABLE) ||
            (pok_threads[res].processor_affinity != current_proc)));

  if ((res == index_low) &&
      ((pok_threads[res].state != POK_STATE_RUNNABLE) ||
       (pok_threads[res].processor_affinity != current_proc))) {
    res = IDLE_THREAD;
  }

#ifdef POK_NEEDS_DEBUG
  if (res != IDLE_THREAD || current_thread != IDLE_THREAD) {
    if (res == IDLE_THREAD) {
      printf("--- Scheduling processor: %hhd\n    scheduling idle thread\n\t\t",
             current_proc);
    } else {
      printf("--- Scheduling processor: %hhd\n    scheduling thread: %d {%lld} "
             "--- ",
             current_proc, res, pok_threads[res].period);
      from = index_low;
      while (from <= index_high) {
        if (pok_threads[from].state == POK_STATE_RUNNABLE &&
            pok_threads[from].processor_affinity == current_proc) {
          printf(" %d {%lld} ,", from, pok_threads[from].period);
        }
        from++;
      }
      printf(" are runnable; \n\t\t");
    }
    from = index_low;
    while (from <= index_high) {
      if (pok_threads[from].state != POK_STATE_RUNNABLE &&
          pok_threads[from].processor_affinity == current_proc) {
        printf(" %d (%s)", from, state_names[pok_threads[from].state]);
      }
      from++;
    }
    printf(" are NOT runnable;\n");
  }
#endif

  return res;
}
#endif /* POK_NEEDS_SCHED_RMS */

#ifdef POK_NEEDS_SCHED_STATIC
uint32_t pok_sched_part_static(const uint32_t index_low,
                               const uint32_t index_high,
                               const uint32_t prev_thread,
                               const uint32_t current_thread) {
  uint32_t from = current_thread != IDLE_THREAD ? current_thread : prev_thread;
  int32_t max_prio = -1;
  uint32_t max_thread = current_thread;
  uint8_t current_proc = pok_get_proc_id();

  if (prev_thread == IDLE_THREAD)
    from = index_low;

  uint32_t i = from;
  do {
    if (pok_threads[i].state == POK_STATE_RUNNABLE &&
        pok_threads[i].processor_affinity == current_proc &&
        pok_threads[i].priority > max_prio) {
      max_prio = pok_threads[i].priority;
      max_thread = i;
    }
    i++;
    if (i >= index_high) {
      i = index_low;
    }
  } while (i != from);

  uint32_t elected = max_prio >= 0 ? max_thread : IDLE_THREAD;

#ifdef POK_NEEDS_DEBUG
  if (elected != current_thread &&
      (elected != IDLE_THREAD || current_thread != IDLE_THREAD)) {
    uint32_t non_ready = 0;
    if (elected == IDLE_THREAD) {
      printf("--- Scheduling processor: %hhd\n    scheduling idle thread\n",
             current_proc);
      non_ready = index_high - index_low;
    } else {
      uint32_t first = 1;
      printf("--- Scheduling processor: %hhd\n    scheduling thread %d "
             "(priority "
             "%d)\n",
             current_proc, elected, pok_threads[elected].priority);
      for (uint32_t i = index_low; i < index_high; i++) {
        if (pok_threads[i].state == POK_STATE_RUNNABLE &&
            pok_threads[i].processor_affinity == current_proc) {
          if (i != elected) {
            printf("%s %d (%d)", first ? "    other ready: " : ",", i,
                   pok_threads[i].priority);
            first = 0;
          }
        } else {
          non_ready++;
        }
      }
      if (!first) {
        printf("\n");
      }
    }
    if (non_ready) {
      printf("    non-ready:");
      uint32_t first = 1;
      for (uint32_t i = index_low; i < index_high; i++) {
        if (pok_threads[i].state != POK_STATE_RUNNABLE &&
            pok_threads[i].processor_affinity == current_proc) {
          printf("%s %d (%d/%s)", first ? "" : ",", i, pok_threads[i].priority,
                 state_names[pok_threads[i].state]);
          first = 0;
        }
      }
      printf("\n");
    }
  }
#endif
  return elected;
}
#endif // POK_NEEDS_SCHED_STATIC

uint32_t pok_sched_part_rr(const uint32_t index_low, const uint32_t index_high,
                           const uint32_t prev_thread,
                           const uint32_t current_thread) {
  uint32_t elected;
  uint32_t from;
  uint8_t current_proc = pok_get_proc_id();

  if (current_thread == IDLE_THREAD) {
    elected = (prev_thread != IDLE_THREAD) ? prev_thread : index_low;
  } else {
    elected = current_thread;
  }

  from = elected;

  if ((pok_threads[current_thread].remaining_time_capacity > 0 ||
       pok_threads[current_thread].time_capacity == INFINITE_TIME_VALUE) &&
      (pok_threads[current_thread].state == POK_STATE_RUNNABLE) &&
      (pok_threads[current_thread].processor_affinity == current_proc) &&
      current_thread != IDLE_THREAD) {
    return current_thread;
  }

  do {
    elected++;
    if (elected >= index_high) {
      elected = index_low;
    }
  } while ((elected != from) &&
           ((pok_threads[elected].state != POK_STATE_RUNNABLE) ||
            (pok_threads[elected].processor_affinity != current_proc)));

  if ((elected == from) &&
      ((pok_threads[elected].state != POK_STATE_RUNNABLE) ||
       (pok_threads[elected].processor_affinity != current_proc))) {
    elected = IDLE_THREAD;
  }

#ifdef POK_NEEDS_DEBUG
  if (elected != current_thread &&
      (elected != IDLE_THREAD || current_thread != IDLE_THREAD)) {
    printf("--- scheduling partition: %d, low:%d, high:%d\n",
           pok_current_partition, index_low, index_high);
    uint32_t non_ready = 0;
    if (elected == IDLE_THREAD) {
      printf("--- Scheduling processor: %hhd\n    scheduling idle thread\n",
             current_proc);
      non_ready = index_high - index_low;
    } else {
      uint32_t first = 1;
      printf("--- Scheduling processor: %hhd\n    scheduling thread %d "
             "(priority "
             "%d)\n",
             current_proc, elected, pok_threads[elected].priority);
      for (uint32_t i = index_low; i < index_high; i++) {
        if (pok_threads[i].state == POK_STATE_RUNNABLE &&
            pok_threads[i].processor_affinity == current_proc) {
          if (i != elected) {
            printf("%s %d (%d)", first ? "    other ready: " : ",", i,
                   pok_threads[i].priority);
            first = 0;
          } else
            printf("elected %d !!! \n", elected);
        } else {
          non_ready++;
        }
      }
      if (!first) {
        printf("\n");
      }
    }
    if (non_ready) {
      printf("    non-ready:");
      uint32_t first = 1;
      for (uint32_t i = index_low; i < index_high; i++) {
        if (pok_threads[i].state != POK_STATE_RUNNABLE &&
            pok_threads[i].processor_affinity == current_proc) {
          printf("%s %d (%d/%s)", first ? "" : ",", i, pok_threads[i].priority,
                 state_names[pok_threads[i].state]);
          first = 0;
        }
      }
      printf("\n");
    }
  }
#endif
  return elected;
}

#if defined(POK_NEEDS_LOCKOBJECTS) || defined(POK_NEEDS_PORTS_QUEUEING) ||     \
    defined(POK_NEEDS_PORTS_SAMPLING)
void pok_sched_unlock_thread(const uint32_t thread_id) {
  pok_threads[thread_id].state = POK_STATE_RUNNABLE;
}
#endif

#if defined(POK_NEEDS_LOCKOBJECTS) || defined(POK_NEEDS_PORTS_QUEUEING) ||     \
    defined(POK_NEEDS_PORTS_SAMPLING) || defined(POK_NEEDS_THREAD_SLEEP) ||    \
    defined(POK_NEEDS_THREAD_SLEEP_UNTIL)
void pok_sched_lock_current_thread(void) {
  pok_threads[POK_SCHED_CURRENT_THREAD].state = POK_STATE_LOCK;
}

void pok_sched_lock_current_thread_timed(const uint64_t time) {
  pok_threads[POK_SCHED_CURRENT_THREAD].state = POK_STATE_WAITING;
  pok_threads[POK_SCHED_CURRENT_THREAD].wakeup_time = time;
}
#endif

void pok_sched_stop_self(void) {
  POK_CURRENT_THREAD.state = POK_STATE_STOPPED;
  pok_sched_thread(TRUE);

  // Never return to stopped thread - infinite loop
  while (1) {
    // Should never reach here after context switch
    pok_sched_thread(FALSE);
  }
}

void pok_sched_stop_thread(const uint32_t tid) {
  pok_threads[tid].state = POK_STATE_STOPPED;
}

#ifdef POK_NEEDS_DEPRECIATED
void pok_sched_lock_thread(const uint32_t thread_id) {
  pok_threads[thread_id].state = POK_STATE_LOCK;
}
#endif

pok_ret_t pok_sched_end_period() {
  POK_CURRENT_THREAD.state = POK_STATE_WAIT_NEXT_ACTIVATION;
  POK_CURRENT_THREAD.remaining_time_capacity = 0;
  pok_sched_thread(TRUE);
  return POK_ERRNO_OK;
}

void pok_sched_activate_error_thread(void) {
  uint32_t error_thread = pok_partitions[pok_current_partition].thread_error;
  if (error_thread != 0) {
    pok_threads[error_thread].priority = pok_sched_get_priority_max(0);
    pok_threads[error_thread].remaining_time_capacity = 1000;
    pok_threads[error_thread].period = 100;
    pok_threads[error_thread].next_activation = 0;

    pok_threads[error_thread].state = POK_STATE_RUNNABLE;
    pok_sched_context_switch(error_thread, TRUE);
  }
}

uint32_t pok_sched_get_current(uint32_t *thread_id) {
  if (pok_partitions[pok_current_partition].thread_error == 0)
    return POK_ERRNO_THREAD;
  if (KERNEL_THREAD == POK_SCHED_CURRENT_THREAD ||
      IDLE_THREAD == POK_SCHED_CURRENT_THREAD) {
    return POK_ERRNO_THREAD;
  }
  *thread_id = POK_SCHED_CURRENT_THREAD;
  return POK_ERRNO_OK;
}

/* Main scheduler entry point called from timer interrupts */
void pok_sched(void) {
  /* Do not schedule until initialization is complete */
  if (!pok_sched_initialized) {
    return;
  }

  /* Check if we're in multiprocessing mode or single processor */
#if POK_CONFIG_NB_PROCESSORS > 1
  /* Multi-processor scheduling */
  pok_global_sched();
#else
  /* Single processor scheduling - check partition election first */
  uint8_t elected_partition = pok_elect_partition();
  new_partition = elected_partition != POK_SCHED_CURRENT_PARTITION;
  POK_SCHED_CURRENT_PARTITION = elected_partition;

  /* Then do thread scheduling within the elected partition */
  pok_sched_thread(TRUE);
#endif
}

/* End of scheduler implementation - now available for ARM */
