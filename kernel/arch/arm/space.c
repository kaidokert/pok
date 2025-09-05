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

/* POK system headers */
#include <errno.h>
#include <libc.h>
#include <types.h>

/* POK core headers */
#include <bsp.h>
#include <core/sched.h>

/* Architecture-specific headers */
#include "arch.h"
#include "cortex_m_config.h"
#include "mpu.h"
#include "mpu_utils.h"
#include "thread.h"

#define KERNEL_STACK_SIZE 4096
#define MEMORY_WASTE_THRESHOLD_PERCENT 25
#define MEMORY_WASTE_CRITICAL_PERCENT 50  /* Fail allocation if waste exceeds this */
#define STACK_ALIGNMENT_BYTES CORTEX_M_STACK_ALIGNMENT  /* ARM Cortex-M requires 8-byte stack alignment */
#define STACK_ALIGNMENT_MASK CORTEX_M_STACK_ALIGNMENT_MASK

/* Helper function to align address down while keeping it within bounds */
static inline uint32_t align_down(uint32_t value, uint32_t alignment) {
    return value & ~(alignment - 1);
}

/* Partition space information */
struct pok_space {
  uint32_t phys_base;
  uint32_t size;
  uint8_t mpu_region;
};

struct pok_space spaces[POK_CONFIG_NB_PARTITIONS];

/**
 * Create a memory space for a partition using MPU protection
 *
 * @param partition_id ID of the partition (0 to POK_CONFIG_NB_PARTITIONS-1)
 * @param addr Base address of the partition memory space
 * @param size Size of the partition memory space
 * @return POK_ERRNO_OK on success, error code on failure
 */
pok_ret_t pok_create_space(uint8_t partition_id, uint32_t addr, uint32_t size) {
  uint32_t mpu_attributes;
  uint8_t region_id;

  if (partition_id >= POK_CONFIG_NB_PARTITIONS) {
    return POK_ERRNO_EINVAL;
  }

  /* Use partition_id + 1 as region ID (reserve region 0 for kernel) */
  region_id = partition_id + 1;

  if (region_id >= pok_mpu_get_region_count()) {
    return POK_ERRNO_EINVAL;
  }

  /* Configure MPU attributes for partition space - enforce W^X principle:
   * - Read/Write access for data region (no execute)
   * - Read/Execute access for code region (no write)
   * - Normal memory with caching
   * Note: This creates a data region first, code region will be separate
   */
  /* Use helper macro for partition data region (read-write, no execute) */
  mpu_attributes = MPU_CONFIG_SRAM_DATA;

  /* Align size to power of 2 (MPU requirement) */
  uint32_t aligned_size = mpu_align_size_to_power_of_2(size);

  /* Security fix: clear exposed memory due to MPU alignment */
  uint32_t exposed_memory = aligned_size - size;
  if (exposed_memory > 0) {
    /* Check if memory waste exceeds critical threshold */
    uint32_t waste_percent = (exposed_memory * 100) / size;
    if (waste_percent > MEMORY_WASTE_CRITICAL_PERCENT) {
#ifdef POK_NEEDS_DEBUG
      printf("ERROR: Partition %d MPU alignment wastes %u%% memory (%u bytes) - exceeds %u%% limit\n",
             partition_id, waste_percent, exposed_memory, MEMORY_WASTE_CRITICAL_PERCENT);
#endif
      return POK_ERRNO_EINVAL;
    }
    
    /* Clear exposed memory to prevent information disclosure */
    void *exposed_start = (void *)(addr + size);
    memset(exposed_start, 0, exposed_memory);
    
#ifdef POK_NEEDS_DEBUG
    if (waste_percent > MEMORY_WASTE_THRESHOLD_PERCENT) {
      printf("WARNING: Partition %d MPU alignment wastes %u%% memory (%u bytes) - cleared for security\n",
             partition_id, waste_percent, exposed_memory);
    }
#endif
  }

  /* Validate base address alignment matches MPU requirements */
  if (!mpu_is_aligned(addr, aligned_size)) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: Partition %d base addr 0x%x not aligned to size 0x%x\n",
           partition_id, addr, aligned_size);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Configure MPU region for partition with subregion support to hide unused memory */
  if (aligned_size >= ARM_MPU_MIN_SUBREGION_SIZE && (aligned_size - size) >= (size / 4)) {
    /* Use subregions if region is large enough and waste is significant */
    if (pok_mpu_configure_region_with_subregions(region_id, addr, size, aligned_size, mpu_attributes) !=
        POK_ERRNO_OK) {
      return POK_ERRNO_EFAULT;
    }
  } else {
    /* Use standard region configuration for small regions */
    if (pok_mpu_configure_region(region_id, addr, aligned_size, mpu_attributes) !=
        POK_ERRNO_OK) {
      return POK_ERRNO_EFAULT;
    }
  }

  /* Store partition information */
  spaces[partition_id].phys_base = addr;
  spaces[partition_id].size = size;
  spaces[partition_id].mpu_region = region_id;

  /* Initially disable the region */
  pok_mpu_disable_region(region_id);

#ifdef POK_NEEDS_DEBUG
  printf("pok_create_space: %d: %x %x (region %d)\n", partition_id, addr, size,
         region_id);
#endif

  return POK_ERRNO_OK;
}

/**
 * Create a separate code region for a partition to enforce W^X security
 *
 * @param partition_id ID of the partition
 * @param code_addr Base address of the code region within partition
 * @param code_size Size of the code region
 * @return POK_ERRNO_OK on success, error code on failure
 */
pok_ret_t pok_create_code_region(uint8_t partition_id, uint32_t code_addr,
                                 uint32_t code_size) {
  uint32_t mpu_attributes;
  uint8_t region_id;

  if (partition_id >= POK_CONFIG_NB_PARTITIONS) {
    return POK_ERRNO_EINVAL;
  }

  /* Use partition_id + 1 + POK_CONFIG_NB_PARTITIONS as code region ID */
  region_id = partition_id + 1 + POK_CONFIG_NB_PARTITIONS;

  if (region_id >= pok_mpu_get_region_count()) {
    return POK_ERRNO_EINVAL;
  }

  /* Configure MPU attributes for code region - enforce W^X:
   * - Read/Execute only (no write permission)
   * - Normal memory with caching
   */
  /* Use helper macro for code region (read-only, executable) */
  mpu_attributes = MPU_CONFIG_FLASH_CODE;

  /* Align size to power of 2 (MPU requirement) */
  uint32_t aligned_size = mpu_align_size_to_power_of_2(code_size);

  /* Validate base address alignment matches MPU requirements */
  if (!mpu_is_aligned(code_addr, aligned_size)) {
#ifdef POK_NEEDS_DEBUG
    printf(
        "ERROR: Partition %d code region addr 0x%x not aligned to size 0x%x\n",
        partition_id, code_addr, aligned_size);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Configure MPU region for partition code */
  if (pok_mpu_configure_region(region_id, code_addr, aligned_size,
                               mpu_attributes) != POK_ERRNO_OK) {
    return POK_ERRNO_EFAULT;
  }

  /* Initially disable the code region */
  pok_mpu_disable_region(region_id);

#ifdef POK_NEEDS_DEBUG
  printf("pok_create_code_region: partition %d: code addr=0x%x size=0x%x "
         "(region %d)\n",
         partition_id, code_addr, code_size, region_id);
#endif

  return POK_ERRNO_OK;
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

  return POK_ERRNO_OK;
}

uint32_t pok_space_base_vaddr(uint32_t addr) {
  /* ARM Cortex-M uses flat memory model - no virtual addressing */
  return (addr);
}

uint32_t pok_space_context_create(uint8_t partition_id, uint32_t entry_rel,
                                  uint8_t processor_affinity,
                                  uint32_t stack_rel, uint32_t arg1,
                                  uint32_t arg2) {
  context_t *ctx;
  char *stack_addr;
  uint32_t entry_abs, stack_abs;

  if (partition_id >= POK_CONFIG_NB_PARTITIONS) {
    return (0);
  }

  /* ARM Cortex-M is single-core, ignore processor_affinity but validate it */
  (void)processor_affinity; /* Suppress unused parameter warning */

  /* Allocate kernel stack */
  stack_addr = pok_bsp_mem_alloc(KERNEL_STACK_SIZE);
  if (!stack_addr) {
    return (0);
  }

  /* Calculate absolute addresses */
  entry_abs = spaces[partition_id].phys_base + entry_rel;
  stack_abs = spaces[partition_id].phys_base + stack_rel;

  /* Set up context at top of kernel stack */
  ctx = (context_t *)(stack_addr + KERNEL_STACK_SIZE - sizeof(context_t));
  memset(ctx, 0, sizeof(context_t));

  /* Initialize ARM Cortex-M context */
  ctx->r0 = arg1; /* First argument */
  ctx->r1 = arg2; /* Second argument */
  ctx->sp = align_down(stack_abs, STACK_ALIGNMENT_BYTES); /* User stack pointer (8-byte aligned, within bounds) */
  ctx->lr = ARM_EXC_RETURN_THREAD_PSP;  /* Return to Thread mode, use PSP */
  ctx->pc = entry_abs;             /* Entry point */
  ctx->xpsr = 0x01000000;          /* Thumb bit set */

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
  /* Use helper macro for kernel data region */
  uint32_t kernel_attrs = MPU_CONFIG_KERNEL_DATA;
  ret = pok_mpu_configure_region(0, pok_bsp_kernel_base(),
                                 pok_bsp_kernel_size(), kernel_attrs);
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
