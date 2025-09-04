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
 * \file    arch/arm/mpu.c
 * \author  POK team
 * \brief   ARM Cortex-M MPU (Memory Protection Unit) implementation
 */

#include "mpu.h"
#include <errno.h>
#include <libc.h>

static uint8_t mpu_region_count = 0;
static mpu_region_t mpu_regions[MPU_MAX_REGIONS];

/**
 * Initialize the Memory Protection Unit (MPU)
 * Detects available MPU regions and sets up initial configuration
 * 
 * @return POK_ERRNO_OK on success, POK_ERRNO_UNAVAILABLE if no MPU present
 */
pok_ret_t pok_mpu_init(void) {
  uint32_t mpu_type;
  
  /* Read MPU Type register to get number of regions */
  mpu_type = MPU_TYPE;
  mpu_region_count = (mpu_type >> 8) & 0xFF;
  
  if (mpu_region_count == 0) {
    /* No MPU present */
    return (POK_ERRNO_UNAVAILABLE);
  }
  
  if (mpu_region_count > MPU_MAX_REGIONS) {
    mpu_region_count = MPU_MAX_REGIONS;
  }
  
  /* Disable MPU during configuration */
  pok_mpu_disable();
  
  /* Clear all regions */
  memset(mpu_regions, 0, sizeof(mpu_regions));
  
  for (uint8_t i = 0; i < mpu_region_count; i++) {
    pok_mpu_disable_region(i);
  }
  
  /* Enable MPU with default memory map for privileged access */
  pok_mpu_enable();
  
  return (POK_ERRNO_OK);
}

/**
 * Configure an MPU region with specified protection attributes
 * 
 * @param region MPU region number (0-7 typically)
 * @param base_addr Base address of the region (must be aligned to size)
 * @param size Size of the region (must be power of 2, minimum 32 bytes)
 * @param attributes Access permissions and memory attributes
 * @return POK_ERRNO_OK on success, error code on failure
 */
pok_ret_t pok_mpu_configure_region(uint8_t region, uint32_t base_addr, 
                                   uint32_t size, uint32_t attributes) {
  uint32_t rasr;
  
  if (region >= mpu_region_count) {
    return (POK_ERRNO_EINVAL);
  }
  
  /* Ensure base address is aligned to size */
  if ((base_addr & (size - 1)) != 0) {
    return (POK_ERRNO_EINVAL);
  }
  
  /* Select region */
  MPU_RNR = region;
  
  /* Configure base address */
  MPU_RBAR = base_addr | MPU_RBAR_VALID | region;
  
  /* Configure attributes and size */
  rasr = pok_mpu_size_to_rasr(size) | attributes | MPU_RASR_ENABLE;
  MPU_RASR = rasr;
  
  /* Store configuration */
  mpu_regions[region].base_addr = base_addr;
  mpu_regions[region].size = size;
  mpu_regions[region].attributes = attributes;
  mpu_regions[region].region_id = region;
  mpu_regions[region].enabled = 1;
  
  /* Data Synchronization Barrier */
  __asm volatile ("dsb" : : : "memory");
  
  return (POK_ERRNO_OK);
}

pok_ret_t pok_mpu_enable_region(uint8_t region) {
  if (region >= mpu_region_count) {
    return (POK_ERRNO_EINVAL);
  }
  
  if (!mpu_regions[region].enabled) {
    MPU_RNR = region;
    MPU_RASR |= MPU_RASR_ENABLE;
    mpu_regions[region].enabled = 1;
    
    __asm volatile ("dsb" : : : "memory");
  }
  
  return (POK_ERRNO_OK);
}

pok_ret_t pok_mpu_disable_region(uint8_t region) {
  if (region >= mpu_region_count) {
    return (POK_ERRNO_EINVAL);
  }
  
  MPU_RNR = region;
  MPU_RASR &= ~MPU_RASR_ENABLE;
  mpu_regions[region].enabled = 0;
  
  __asm volatile ("dsb" : : : "memory");
  
  return (POK_ERRNO_OK);
}

pok_ret_t pok_mpu_enable(void) {
  /* Enable MPU with background region for privileged access */
  MPU_CTRL = MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA;
  
  /* Data Synchronization Barrier */
  __asm volatile ("dsb" : : : "memory");
  /* Instruction Synchronization Barrier */
  __asm volatile ("isb" : : : "memory");
  
  return (POK_ERRNO_OK);
}

pok_ret_t pok_mpu_disable(void) {
  /* Disable MPU */
  MPU_CTRL = 0;
  
  __asm volatile ("dsb" : : : "memory");
  __asm volatile ("isb" : : : "memory");
  
  return (POK_ERRNO_OK);
}

inline uint8_t pok_mpu_get_region_count(void) {
  return (mpu_region_count);
}

uint32_t pok_mpu_size_to_rasr(uint32_t size) {
  uint32_t rasr_size = 0;
  
  /* Size must be power of 2 and >= 32 bytes */
  if (size < 32 || (size & (size - 1)) != 0) {
    return (0); /* Invalid size */
  }
  
  /* Calculate size field (log2(size) - 1) */
  while (size > 1) {
    size >>= 1;
    rasr_size++;
  }
  rasr_size--;
  
  return (rasr_size << MPU_RASR_SIZE_SHIFT) & MPU_RASR_SIZE_MASK;
}