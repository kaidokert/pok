/*
 * ARM compatibility layer for kernel functions
 * Compiled with same flags as kernel to avoid ARM/Thumb issues
 */

#include <core/sched.h>
#include <errno.h>
#include <libc.h>
#include <types.h>

/* Forward declaration */
extern pok_ret_t pok_timer_init(void);

/* BSP time initialization - kernel compatible version */
pok_ret_t pok_bsp_time_init(void) {
  /* Actually call the timer initialization */
  pok_ret_t ret = pok_timer_init();
  return ret;
}

/* Scheduler election function - single-core ARM implementation */
uint8_t pok_sched_election(void) {
  /* Forward declaration - defined in kernel/core/sched.c */
  extern uint8_t pok_elect_partition(void);
  extern uint8_t POK_SCHED_CURRENT_PARTITION;

  /* Elect next partition based on timeslot scheduling */
  uint8_t elected_partition = pok_elect_partition();

  /* Update current partition if changed */
  if (elected_partition != POK_SCHED_CURRENT_PARTITION) {
    POK_SCHED_CURRENT_PARTITION = elected_partition;
  }

  return elected_partition;
}
