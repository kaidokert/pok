/* Generic ARM timer placeholder; real targets override with strong symbols. */
#include <stdint.h>

__attribute__((weak)) void pok_timer_init(void) { /* no-op */ }
__attribute__((weak)) void pok_timer_handler(void) { /* no-op */ }