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

#ifndef __POK_ARM_MEMORY_CONFIG_H__
#define __POK_ARM_MEMORY_CONFIG_H__

/**
 * \file    arch/arm/memory_config.h
 * \brief   Configurable memory layout definitions for ARM platforms
 * \author  POK team
 * 
 * This file provides configurable memory layout constants that can be
 * overridden per BSP to support different ARM Cortex-M hardware configurations.
 */

/* Default memory configuration - can be overridden by BSP-specific headers */

#ifndef POK_FLASH_BASE
#define POK_FLASH_BASE    0x08000000  /* Default STM32F4 flash base */
#endif

#ifndef POK_SRAM_BASE
#define POK_SRAM_BASE     0x20000000  /* Default STM32F4 SRAM base */
#endif

#ifndef POK_SRAM_SIZE
#define POK_SRAM_SIZE     0x20000     /* Default 128KB SRAM */
#endif

#ifndef POK_KERNEL_MEMORY_SIZE
#define POK_KERNEL_MEMORY_SIZE  0x8000  /* Default 32KB for kernel */
#endif

/* Derived memory layout */
#define POK_KERNEL_MEMORY_BASE    POK_SRAM_BASE
#define POK_USER_MEMORY_BASE      (POK_SRAM_BASE + POK_KERNEL_MEMORY_SIZE)
#define POK_USER_MEMORY_SIZE      (POK_SRAM_SIZE - POK_KERNEL_MEMORY_SIZE)

/* Memory alignment constants */
#define POK_MEMORY_ALIGNMENT 8
#define POK_MEMORY_ALIGNMENT_MASK 7

/* Kernel region size for MPU configuration */
#ifndef POK_KERNEL_REGION_SIZE
#define POK_KERNEL_REGION_SIZE    POK_KERNEL_MEMORY_SIZE
#endif

#endif /* !__POK_ARM_MEMORY_CONFIG_H__ */
