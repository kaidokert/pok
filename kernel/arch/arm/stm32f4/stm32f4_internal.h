/*
 * Internal function prototypes for STM32F4 BSP implementation
 */

#ifndef __STM32F4_INTERNAL_H__
#define __STM32F4_INTERNAL_H__

#include <errno.h>
#include <types.h>

/* Internal BSP functions */
pok_ret_t pok_cons_init(void);
pok_ret_t pok_timer_init(void);
void pok_timer_handler(void);

#endif /* !__STM32F4_INTERNAL_H__ */
