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

#ifndef __POK_ARM_CORTEX_M_CONFIG_H__
#define __POK_ARM_CORTEX_M_CONFIG_H__

/**
 * \file    arch/arm/cortex_m_config.h
 * \brief   ARM Cortex-M configuration constants and limits
 * \author  POK team
 *
 * This file centralizes hardware-specific constants for ARM Cortex-M
 * microcontrollers to make it easier to support different variants.
 */

/* NVIC (Nested Vectored Interrupt Controller) configuration */
#ifndef CORTEX_M_NVIC_VECTOR_COUNT
#define CORTEX_M_NVIC_VECTOR_COUNT                                             \
  98 /* 16 system + 82 external interrupts for STM32F4 */
#endif

#define CORTEX_M_NVIC_VECTOR_TABLE_SIZE                                        \
  (CORTEX_M_NVIC_VECTOR_COUNT * 4) /* Each vector is 4 bytes */

/* Calculate next power of 2 for vector table alignment */
#ifndef CORTEX_M_NVIC_VECTOR_TABLE_ALIGNMENT
#define CORTEX_M_NVIC_VECTOR_TABLE_ALIGNMENT                                   \
  512 /* Next power of 2 above vector table size */
#endif

/* MPU (Memory Protection Unit) configuration */
#ifndef CORTEX_M_MPU_MAX_REGIONS
#define CORTEX_M_MPU_MAX_REGIONS                                               \
  8 /* Standard Cortex-M3/M4 has 8 MPU regions                                 \
     */
#endif

#ifndef CORTEX_M_MPU_MIN_REGION_SIZE
#define CORTEX_M_MPU_MIN_REGION_SIZE 32 /* Minimum MPU region size in bytes */
#endif

/* Thread/Stack configuration */
#ifndef CORTEX_M_STACK_ALIGNMENT
#define CORTEX_M_STACK_ALIGNMENT                                               \
  8 /* ARM Cortex-M requires 8-byte stack alignment */
#endif

#define CORTEX_M_STACK_ALIGNMENT_MASK (CORTEX_M_STACK_ALIGNMENT - 1)

/* Exception priorities (0 = highest, 255 = lowest for Cortex-M3/M4) */
#ifndef CORTEX_M_PRIORITY_HIGHEST
#define CORTEX_M_PRIORITY_HIGHEST 0
#endif

#ifndef CORTEX_M_PRIORITY_HIGH
#define CORTEX_M_PRIORITY_HIGH 64
#endif

#ifndef CORTEX_M_PRIORITY_NORMAL
#define CORTEX_M_PRIORITY_NORMAL 128
#endif

#ifndef CORTEX_M_PRIORITY_LOW
#define CORTEX_M_PRIORITY_LOW 192
#endif

#ifndef CORTEX_M_PRIORITY_LOWEST
#define CORTEX_M_PRIORITY_LOWEST 255
#endif

#endif /* !__POK_ARM_CORTEX_M_CONFIG_H__ */
