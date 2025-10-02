/*
 * ARM SMP stub implementations for single-core STM32F4
 * These functions are required by the kernel but not used in single-core ARM
 * systems
 */

#include <types.h>

/* IPI (Inter-Processor Interrupt) stubs - not needed for single-core ARM */
void __attribute__((target("thumb"))) pok_end_ipi(void) {
  /* No-op for single-core ARM */
  asm volatile("nop");
}

void __attribute__((target("thumb"))) pok_send_global_schedule_thread(void) {
  /* No-op for single-core ARM */
  asm volatile("nop");
}

void __attribute__((target("thumb")))
pok_send_schedule_thread(uint8_t processor) {
  /* No-op for single-core ARM */
  (void)processor;
  asm volatile("nop");
}

void __attribute__((target("thumb")))
pok_send_schedule_thread_other_processors(void) {
  /* No-op for single-core ARM */
  asm volatile("nop");
}

/* NOTE: pok_bsp_time_init and pok_sched_election are implemented in timer.c */
