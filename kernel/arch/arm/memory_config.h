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
#define POK_FLASH_BASE 0x08000000UL /* Default STM32F4 flash base */
#endif

#ifndef POK_SRAM_BASE
#define POK_SRAM_BASE 0x20000000UL /* Default STM32F4 SRAM base */
#endif

#ifndef POK_SRAM_SIZE
#define POK_SRAM_SIZE 0x20000UL /* Default 128KB SRAM */
#endif

#ifndef POK_KERNEL_MEMORY_SIZE
#define POK_KERNEL_MEMORY_SIZE 0x8000UL /* Default 32KB for kernel */
#endif

/* Derived memory layout */
#define POK_KERNEL_MEMORY_BASE POK_SRAM_BASE
#define POK_USER_MEMORY_BASE (POK_SRAM_BASE + POK_KERNEL_MEMORY_SIZE)
#define POK_USER_MEMORY_SIZE (POK_SRAM_SIZE - POK_KERNEL_MEMORY_SIZE)

/* Compile-time guards for memory layout assumptions */
#if POK_KERNEL_MEMORY_SIZE >= POK_SRAM_SIZE
#error "POK_KERNEL_MEMORY_SIZE must be smaller than POK_SRAM_SIZE"
#endif

#if POK_KERNEL_MEMORY_SIZE == 0
#error "POK_KERNEL_MEMORY_SIZE cannot be zero"
#endif

#if POK_SRAM_SIZE == 0
#error "POK_SRAM_SIZE cannot be zero"
#endif

#if (POK_SRAM_BASE & (POK_MEMORY_ALIGNMENT - 1)) != 0
#error "POK_SRAM_BASE must be aligned to POK_MEMORY_ALIGNMENT"
#endif

#if (POK_KERNEL_MEMORY_SIZE & (POK_MEMORY_ALIGNMENT - 1)) != 0
#error "POK_KERNEL_MEMORY_SIZE must be aligned to POK_MEMORY_ALIGNMENT"
#endif

/* Ensure minimum viable memory sizes */
#if POK_KERNEL_MEMORY_SIZE < 0x2000UL
#error "POK_KERNEL_MEMORY_SIZE must be at least 8KB for basic kernel operation"
#endif

#if POK_USER_MEMORY_SIZE < 0x1000UL
#error "Insufficient user memory - need at least 4KB for partitions"
#endif

/* MPU subregion alignment warning - kernel size should align for optimal MPU
 * usage */
#if (POK_KERNEL_MEMORY_SIZE & MPU_SUBREGION_ALIGNMENT_MASK) != 0
#warning                                                                       \
    "POK_KERNEL_MEMORY_SIZE not aligned to MPU subregion boundary (32 bytes) - may reduce MPU efficiency"
#endif

/* Memory alignment constants */
#define POK_MEMORY_ALIGNMENT 8
#define POK_MEMORY_ALIGNMENT_MASK 7

/* ARM MPU subregion alignment - minimum 32 bytes for granular protection */
#define MPU_SUBREGION_SIZE 32
#define MPU_SUBREGION_ALIGNMENT_MASK (MPU_SUBREGION_SIZE - 1)

/* Kernel region size for MPU configuration */
#ifndef POK_KERNEL_REGION_SIZE
#define POK_KERNEL_REGION_SIZE POK_KERNEL_MEMORY_SIZE
#endif

#endif /* !__POK_ARM_MEMORY_CONFIG_H__ */
