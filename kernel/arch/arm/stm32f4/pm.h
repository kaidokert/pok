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
 * \file    arch/arm/stm32f4/pm.h
 * \author  POK team
 * \brief   ARM STM32F4 Physical Memory Management Header
 */

#ifndef __POK_ARM_STM32F4_PM_H__
#define __POK_ARM_STM32F4_PM_H__

#include <types.h>

extern uint32_t pok_arm_pm_heap_start;
extern uint32_t pok_arm_pm_brk;
extern uint32_t pok_arm_pm_heap_end;

int pok_pm_init(void);
uint32_t pok_pm_sbrk(uint32_t increment);

#endif /* __POK_ARM_STM32F4_PM_H__ */
