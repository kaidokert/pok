/* Generic ARM timer placeholder; real targets override with strong symbols. */
#include <errno.h>
#include <stdint.h>
#include <types.h>

__attribute__((weak)) pok_ret_t pok_timer_init(void) { return POK_ERRNO_OK; }
__attribute__((weak)) void pok_timer_handler(void) { /* no-op */ }
