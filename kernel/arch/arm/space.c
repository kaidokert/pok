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
 * \file    arch/arm/space.c
 * \brief   ARM Cortex-M address space management using MPU
 * \author  POK team
 */

#include <bsp.h>
#include <core/sched.h>
#include <errno.h>
#include <libc.h>
#include <types.h>

#include <arch.h>
#include "mpu.h"
#include "thread.h"

#define KERNEL_STACK_SIZE 4096

/* Partition space information */
struct pok_space {
  uint32_t phys_base;
  uint32_t size;
  uint8_t  mpu_region;
};

struct pok_space spaces[POK_CONFIG_NB_PARTITIONS];

pok_ret_t pok_create_space(uint8_t partition_id, uint32_t addr, uint32_t size) {
  uint32_t mpu_attributes;
  uint8_t region_id;
  
  if (partition_id >= POK_CONFIG_NB_PARTITIONS) {
    return (POK_ERRNO_EINVAL);
  }
  
  /* Use partition_id + 1 as region ID (reserve region 0 for kernel) */
  region_id = partition_id + 1;
  
  if (region_id >= pok_mpu_get_region_count()) {
    return (POK_ERRNO_EINVAL);
  }
  
  /* Configure MPU attributes for partition space:
   * - Read/Write access for all
   * - Execute permission for code
   * - Normal memory with caching
   */
  mpu_attributes = (MPU_AP_ALL_RW << MPU_RASR_AP_SHIFT) | MPU_ATTR_NORMAL;
  
  /* Align size to power of 2 (MPU requirement) */
  uint32_t aligned_size = 32;
  while (aligned_size < size) {
    aligned_size <<= 1;
  }
  
  /* Security check: warn if alignment exposes significant unused memory */
  uint32_t exposed_memory = aligned_size - size;
  if (exposed_memory > (size / 4)) {  /* More than 25% waste */
#ifdef POK_NEEDS_DEBUG
    printf("WARNING: Partition %d MPU alignment exposes %u bytes of unused memory\n", 
           partition_id, exposed_memory);
#endif
  }
  
  /* Validate base address alignment matches MPU requirements */
  if ((addr & (aligned_size - 1)) != 0) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: Partition %d base addr 0x%x not aligned to size 0x%x\n",
           partition_id, addr, aligned_size);
#endif
    return (POK_ERRNO_EINVAL);
  }
  
  /* Configure MPU region for partition */
  if (pok_mpu_configure_region(region_id, addr, aligned_size, mpu_attributes) != POK_ERRNO_OK) {
    return (POK_ERRNO_EFAULT);
  }
  
  /* Store partition information */
  spaces[partition_id].phys_base = addr;
  spaces[partition_id].size = size;
  spaces[partition_id].mpu_region = region_id;
  
  /* Initially disable the region */
  pok_mpu_disable_region(region_id);
  
#ifdef POK_NEEDS_DEBUG
  printf("pok_create_space: %d: %x %x (region %d)\n", 
         partition_id, addr, size, region_id);
#endif
  
  return (POK_ERRNO_OK);
}

pok_ret_t pok_space_switch(uint8_t old_partition_id, uint8_t new_partition_id) {
  if (old_partition_id < POK_CONFIG_NB_PARTITIONS) {
    /* Disable old partition's MPU region */
    pok_mpu_disable_region(spaces[old_partition_id].mpu_region);
  }
  
  if (new_partition_id < POK_CONFIG_NB_PARTITIONS) {
    /* Enable new partition's MPU region */
    pok_mpu_enable_region(spaces[new_partition_id].mpu_region);
  }
  
  return (POK_ERRNO_OK);
}

uint32_t pok_space_base_vaddr(uint32_t addr) {
  /* ARM Cortex-M uses flat memory model - no virtual addressing */
  return addr;
}

uint32_t pok_space_context_create(uint8_t partition_id, uint32_t entry_rel,
                                  uint32_t stack_rel, uint32_t arg1,
                                  uint32_t arg2) {
  context_t *ctx;
  char *stack_addr;
  uint32_t entry_abs, stack_abs;
  
  if (partition_id >= POK_CONFIG_NB_PARTITIONS) {
    return 0;
  }
  
  /* Allocate kernel stack */
  stack_addr = pok_bsp_mem_alloc(KERNEL_STACK_SIZE);
  if (!stack_addr) {
    return 0;
  }
  
  /* Calculate absolute addresses */
  entry_abs = spaces[partition_id].phys_base + entry_rel;
  stack_abs = spaces[partition_id].phys_base + stack_rel;
  
  /* Set up context at top of kernel stack */
  ctx = (context_t *)(stack_addr + KERNEL_STACK_SIZE - sizeof(context_t));
  memset(ctx, 0, sizeof(context_t));
  
  /* Initialize ARM Cortex-M context */
  ctx->r0 = arg1;                    /* First argument */
  ctx->r1 = arg2;                    /* Second argument */
  ctx->sp = stack_abs & ~0x7u;       /* User stack pointer (8-byte aligned) */
  ctx->lr = 0xFFFFFFFD;              /* Return to Thread mode, use PSP */
  ctx->pc = entry_abs;               /* Entry point */
  ctx->xpsr = 0x01000000;            /* Thumb bit set */
  
#ifdef POK_NEEDS_DEBUG
  printf("space_context_create %d: entry=%x stack=%x arg1=%x arg2=%x ksp=%x\n",
         partition_id, entry_abs, stack_abs, arg1, arg2, (uint32_t)ctx);
#endif
  
  return (uint32_t)ctx;
}

pok_ret_t pok_arch_space_init(void) {
  pok_ret_t ret;
  
  /* Initialize partition spaces array */
  memset(spaces, 0, sizeof(spaces));
  
  /* Reserve region 0 for kernel space */
  uint32_t kernel_attrs = (MPU_AP_PRIV_RW << MPU_RASR_AP_SHIFT) | MPU_ATTR_NORMAL;
  ret = pok_mpu_configure_region(0, pok_bsp_kernel_base(), pok_bsp_kernel_size(), kernel_attrs);
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: Failed to configure kernel MPU region: %d\n", ret);
#endif
    return ret;
  }
  
#ifdef POK_NEEDS_DEBUG
  printf("pok_arch_space_init: MPU regions=%d\n", pok_mpu_get_region_count());
#endif
  
  return POK_ERRNO_OK;
}