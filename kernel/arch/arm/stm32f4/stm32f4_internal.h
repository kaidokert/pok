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
 * \file    arch/arm/stm32f4/stm32f4_internal.h
 * \author  POK team
 * \brief   Internal function prototypes for STM32F4 BSP implementation
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
