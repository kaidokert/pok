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

/* POK system headers */
#include <errno.h>
#include <libc.h>

/* POK core headers */
#include <core/partition.h>

/* Architecture-specific headers */
#include "arch.h"
#include "mpu.h"

/* ARM Cortex-M intrinsics */
#ifdef __ARM_ARCH
#include <cmsis_gcc.h>
#endif
#include "mpu_utils.h"

static uint8_t mpu_region_count = 0;
static mpu_region_t mpu_regions[MPU_MAX_REGIONS];

/**
 * Check if two memory regions overlap
 *
 * @param base1 Base address of first region
 * @param size1 Size of first region
 * @param base2 Base address of second region
 * @param size2 Size of second region
 * @return TRUE if regions overlap, FALSE otherwise
 */
static pok_bool_t mpu_regions_overlap(uint32_t base1, uint32_t size1,
                                      uint32_t base2, uint32_t size2) {
  if (size1 == 0 || size2 == 0) {
    return FALSE;
  }

  uint64_t end1 = (uint64_t)base1 + (uint64_t)size1; /* half-open end */
  uint64_t end2 = (uint64_t)base2 + (uint64_t)size2;

  if (end1 > ((uint64_t)UINT32_MAX + 1) || end2 > ((uint64_t)UINT32_MAX + 1)) {
    return TRUE;
  }

  /* Use half-open intervals: [base, end) */
  return !((uint64_t)end1 <= (uint64_t)base2 ||
           (uint64_t)end2 <= (uint64_t)base1);
}

/**
 * Check if two memory regions overlap accounting for SubRegion Disable (SRD)
 * masks
 *
 * @param base1 Base address of first region
 * @param size1 Size of first region
 * @param srd1 SRD mask of first region (8 bits)
 * @param base2 Base address of second region
 * @param size2 Size of second region
 * @param srd2 SRD mask of second region (8 bits)
 * @return TRUE if regions overlap, FALSE otherwise
 */
static pok_bool_t mpu_regions_overlap_with_srd(uint32_t base1, uint32_t size1,
                                               uint8_t srd1, uint32_t base2,
                                               uint32_t size2, uint8_t srd2) {
  /* First check if regions overlap at all */
  if (!mpu_regions_overlap(base1, size1, base2, size2)) {
    return FALSE;
  }

  /* If regions are too small for subregions, fall back to basic overlap */
  if (size1 < ARM_MPU_MIN_SUBREGION_SIZE ||
      size2 < ARM_MPU_MIN_SUBREGION_SIZE) {
    return TRUE;
  }

  /* Ensure bases are aligned to their region sizes, otherwise SRD math is
   * invalid */
  if (!mpu_is_aligned(base1, size1) || !mpu_is_aligned(base2, size2)) {
    return TRUE; /* treat as overlapping for safety */
  }

  /* For regions with subregions, check if any enabled subregions actually
   * overlap */
  uint32_t sub1_size = size1 / ARM_MPU_SUBREGION_COUNT;
  uint32_t sub2_size = size2 / ARM_MPU_SUBREGION_COUNT;

  for (int i = 0; i < ARM_MPU_SUBREGION_COUNT; i++) {
    if (srd1 & (1u << i))
      continue; /* disabled subregion */
    uint32_t sub1_base = base1 + (i * sub1_size);
    uint32_t sub1_end = sub1_base + sub1_size;

    for (int j = 0; j < ARM_MPU_SUBREGION_COUNT; j++) {
      if (srd2 & (1u << j))
        continue;
      uint32_t sub2_base = base2 + (j * sub2_size);
      uint32_t sub2_end = sub2_base + sub2_size;

      if ((sub1_base < sub2_end) && (sub2_base < sub1_end)) {
        return TRUE;
      }
    }
  }
  return FALSE;
}

/**
 * Check if two regions belong to the same partition for W^X overlap policy
 *
 * @param region_id1 First region ID
 * @param region_id2 Second region ID
 * @return TRUE if both regions belong to the same partition, FALSE otherwise
 */
static pok_bool_t is_same_partition_regions(uint8_t region_id1,
                                            uint8_t region_id2) {
  /* Validate region IDs are within available hardware regions */
  if (region_id1 >= mpu_region_count || region_id2 >= mpu_region_count) {
    return FALSE;
  }
  /* Region 0 is kernel - never allow overlap with kernel */
  if (region_id1 == 0 || region_id2 == 0) {
    return FALSE;
  }

  /* Extract partition IDs from region IDs using the mapping:
   * - Data regions: partition_id = region_id - 1
   * - Code regions: partition_id = region_id - 1 - POK_CONFIG_NB_PARTITIONS */
  uint8_t partition_id1, partition_id2;

  /* Determine partition for region_id1 */
  if (region_id1 <= POK_CONFIG_NB_PARTITIONS) {
    /* Data region */
    partition_id1 = region_id1 - 1;
  } else {
    /* Code region */
    partition_id1 = region_id1 - 1 - POK_CONFIG_NB_PARTITIONS;
  }

  /* Determine partition for region_id2 */
  if (region_id2 <= POK_CONFIG_NB_PARTITIONS) {
    /* Data region */
    partition_id2 = region_id2 - 1;
  } else {
    /* Code region */
    partition_id2 = region_id2 - 1 - POK_CONFIG_NB_PARTITIONS;
  }

  return (partition_id1 == partition_id2);
}

/**
 * Check if a W^X overlap is valid (code region over data region)
 *
 * @param new_region_id ID of the new region being configured
 * @param existing_region_id ID of the existing region
 * @return TRUE if this is a valid W^X overlap, FALSE otherwise
 */
static pok_bool_t is_valid_wx_overlap(uint8_t new_region_id,
                                      uint8_t existing_region_id) {
  /* Only allow code regions to overlap data regions, not vice versa
   * Code regions have higher region numbers and take priority in ARM MPU */
  pok_bool_t new_is_code = (new_region_id > POK_CONFIG_NB_PARTITIONS);
  pok_bool_t existing_is_data =
      (existing_region_id > 0 &&
       existing_region_id <= POK_CONFIG_NB_PARTITIONS);

  return (new_is_code && existing_is_data);
}

/**
 * Validate that a new region doesn't overlap with existing enabled regions
 *
 * @param new_region_id ID of the region being configured (skip this in check)
 * @param base_addr Base address of new region
 * @param size Size of new region
 * @return POK_ERRNO_OK if no overlap, error code if overlap detected
 */
static pok_ret_t mpu_validate_no_overlap(uint8_t new_region_id,
                                         uint32_t base_addr, uint32_t size) {
  for (uint8_t i = 0; i < MPU_MAX_REGIONS; i++) {
    /* Skip the region we're configuring and truly unconfigured regions
     * Note: Check size == 0 instead of !enabled to include
     * disabled-but-configured regions */
    if (i == new_region_id || mpu_regions[i].size == 0) {
      continue;
    }

    /* Check for overlap considering SubRegion Disable (SRD) masks */
    if (mpu_regions_overlap_with_srd(
            base_addr, size, 0x00, /* New region has no SRD */
            mpu_regions[i].base_addr, mpu_regions[i].size,
            mpu_regions[i].srd_mask)) {

      /* POLICY: Allow controlled same-partition overlaps for W^X enforcement
       * Higher region numbers (code) take priority over lower (data) in ARM MPU
       */
      if (is_same_partition_regions(new_region_id, i) &&
          is_valid_wx_overlap(new_region_id, i)) {
#ifdef POK_NEEDS_DEBUG
        printf("INFO: Allowing W^X overlap - code region %u over data region "
               "%u (same partition)\n",
               new_region_id, i);
#endif
        continue; /* Allow this overlap for W^X enforcement */
      }

      /* Reject all other overlaps */
#ifdef POK_NEEDS_DEBUG
      printf("ERROR: MPU region %u overlaps with existing region %u\n",
             new_region_id, i);
      printf("  New: 0x%08X-0x%08X, Existing: 0x%08X-0x%08X\n", base_addr,
             base_addr + size - 1, mpu_regions[i].base_addr,
             mpu_regions[i].base_addr + mpu_regions[i].size - 1);
      printf("  Same partition: %s, Valid W^X: %s\n",
             is_same_partition_regions(new_region_id, i) ? "YES" : "NO",
             is_valid_wx_overlap(new_region_id, i) ? "YES" : "NO");
#endif
      return POK_ERRNO_EINVAL;
    }
  }

  return POK_ERRNO_OK;
}

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
  mpu_region_count = (mpu_type >> 8) & ARM_REGISTER_BYTE_MASK;

  if (mpu_region_count == 0) {
    /* No MPU present */
    return POK_ERRNO_UNAVAILABLE;
  }

  if (mpu_region_count > MPU_MAX_REGIONS) {
#ifdef POK_NEEDS_DEBUG
    printf("WARNING: Hardware has %d MPU regions, but POK configured for max "
           "%d. Truncating to %d regions.\n",
           mpu_region_count, MPU_MAX_REGIONS, MPU_MAX_REGIONS);
#endif
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

  return POK_ERRNO_OK;
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
  pok_ret_t ret;

  if (region >= mpu_region_count) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: MPU region %u exceeds available regions (%u)\n", region,
           mpu_region_count);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Validate size */
  if (size < MPU_MIN_REGION_SIZE || !mpu_is_power_of_2(size)) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: MPU region size %u is not power-of-2 or below minimum\n",
           size);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Ensure base address is aligned to size */
  if (!mpu_is_aligned(base_addr, size)) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: MPU region base address 0x%08X not aligned to size %u\n",
           base_addr, size);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Validate attributes don't contain reserved bits or conflicts */
  if (attributes & 0x0000FFFF) {
#ifdef POK_NEEDS_DEBUG
    printf(
        "ERROR: MPU attributes 0x%08X contain reserved bits in lower 16 bits\n",
        attributes);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Check for overlapping regions before configuring */
  ret = mpu_validate_no_overlap(region, base_addr, size);
  if (ret != POK_ERRNO_OK) {
    return ret; /* Error already logged in validation function */
  }

  /* Configure attributes and size before disabling interrupts */
  uint32_t size_field = pok_mpu_size_to_rasr(size);
  if (size_field == 0) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: MPU cannot convert size %u to valid RASR field\n", size);
#endif
    return POK_ERRNO_EINVAL;
  }
  rasr = size_field | attributes | MPU_RASR_ENABLE;

  /* Make MPU region programming atomic w.r.t. interrupts */
  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  /* Select region */
  MPU_RNR = region;

  /* Disable region first to prevent transient faults during reprogramming */
  MPU_RASR = 0;

  /* Configure base address - region already selected via MPU_RNR */
  MPU_RBAR = (base_addr & MPU_RBAR_ADDR_MASK);

  /* Configure attributes and size (with enable bit) */
  MPU_RASR = rasr;

  /* Data Synchronization Barrier */
  __asm volatile("dsb" : : : "memory");

  /* Instruction Synchronization Barrier - ARM recommends ISB after MPU updates
   */
  __asm volatile("isb" : : : "memory");

  /* Restore interrupt state */
  __set_PRIMASK(primask);

  /* Store configuration */
  mpu_regions[region].base_addr = base_addr;
  mpu_regions[region].size = size;
  mpu_regions[region].attributes = attributes;
  mpu_regions[region].region_id = region;
  mpu_regions[region].enabled = 1;
  mpu_regions[region].srd_mask =
      0x00; /* No subregions disabled for regular regions */

  return POK_ERRNO_OK;
}

/**
 * Configure an MPU region with subregion support to mask unused memory
 *
 * @param region MPU region number
 * @param base_addr Base address of the region (must be aligned to size)
 * @param actual_size Actual size needed (not power of 2)
 * @param aligned_size Aligned size (power of 2)
 * @param attributes Access permissions and memory attributes
 * @return POK_ERRNO_OK on success, error code on failure
 */
pok_ret_t pok_mpu_configure_region_with_subregions(uint8_t region,
                                                   uint32_t base_addr,
                                                   uint32_t actual_size,
                                                   uint32_t aligned_size,
                                                   uint32_t attributes) {
  uint32_t rasr;
  uint8_t subregion_disable = 0;

  if (region >= mpu_region_count) {
    return POK_ERRNO_EINVAL;
  }

  /* Validate aligned_size is a valid MPU size */
  if (!MPU_IS_VALID_SIZE(aligned_size)) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: MPU aligned_size %u is not power-of-2 or below minimum\n",
           aligned_size);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Validate actual_size bounds */
  if (actual_size == 0 || actual_size > aligned_size) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: MPU actual_size %u must be > 0 and <= aligned_size %u\n",
           actual_size, aligned_size);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Validate attributes don't contain reserved bits or conflicts */
  if (attributes & 0x0000FFFF) {
#ifdef POK_NEEDS_DEBUG
    printf(
        "ERROR: MPU attributes 0x%08X contain reserved bits in lower 16 bits\n",
        attributes);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Check for overlapping regions before configuring */
  pok_ret_t ret = mpu_validate_no_overlap(region, base_addr, aligned_size);
  if (ret != POK_ERRNO_OK) {
    return ret; /* Error already logged in validation function */
  }

  /* Ensure base address is aligned to size using consistent helper macro */
  if (!mpu_is_aligned(base_addr, aligned_size)) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: MPU region base address 0x%08X not aligned to size %u\n",
           base_addr, aligned_size);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Calculate subregion disable bits to mask unused memory */
  if (aligned_size >=
      ARM_MPU_MIN_SUBREGION_SIZE) { /* Minimum size for subregions */
    uint32_t subregion_size =
        aligned_size / ARM_MPU_SUBREGION_COUNT; /* MPU subregions */
    uint32_t used_subregions =
        (actual_size + subregion_size - 1) / subregion_size;

    /* Disable unused subregions (set corresponding bits) */
    for (uint8_t i = used_subregions; i < ARM_MPU_SUBREGION_COUNT; i++) {
      subregion_disable |= (1 << i);
    }

#ifdef POK_NEEDS_DEBUG
    if (subregion_disable != 0) {
      uint32_t exposed_memory =
          aligned_size - (used_subregions * subregion_size);
      printf("MPU region %d: using subregions to hide %u bytes (mask=0x%02x)\n",
             region, exposed_memory, subregion_disable);
    }
#endif
  }

  /* Validate size before disabling interrupts */
  uint32_t size_field = pok_mpu_size_to_rasr(aligned_size);
  if (size_field == 0) {
    return POK_ERRNO_EINVAL;
  }
  rasr = size_field | attributes | (subregion_disable << MPU_RASR_SRD_SHIFT) |
         MPU_RASR_ENABLE;

  /* Make MPU region programming atomic w.r.t. interrupts */
  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  /* Select region */
  MPU_RNR = region;

  /* Disable region first to prevent transient faults during reprogramming */
  MPU_RASR = 0;

  /* Configure base address - region already selected via MPU_RNR */
  MPU_RBAR = (base_addr & MPU_RBAR_ADDR_MASK);

  /* Configure attributes, size, and subregion disable (with enable bit) */
  MPU_RASR = rasr;

  /* Data Synchronization Barrier */
  __asm volatile("dsb" : : : "memory");

  /* Instruction Synchronization Barrier - ARM recommends ISB after MPU updates
   */
  __asm volatile("isb" : : : "memory");

  /* Restore interrupt state */
  __set_PRIMASK(primask);

  /* Store configuration */
  mpu_regions[region].base_addr = base_addr;
  mpu_regions[region].size = aligned_size;
  mpu_regions[region].attributes = attributes;
  mpu_regions[region].region_id = region;
  mpu_regions[region].enabled = 1;
  mpu_regions[region].srd_mask = subregion_disable;

  return POK_ERRNO_OK;
}

pok_ret_t pok_mpu_enable_region(uint8_t region) {
  if (region >= mpu_region_count) {
    return POK_ERRNO_EINVAL;
  }

  if (!mpu_regions[region].enabled) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    MPU_RNR = region;
    MPU_RASR |= MPU_RASR_ENABLE;
    mpu_regions[region].enabled = 1;

    __asm volatile("dsb" : : : "memory");
    __asm volatile("isb" : : : "memory");

    __set_PRIMASK(primask);
  }

  return POK_ERRNO_OK;
}

pok_ret_t pok_mpu_disable_region(uint8_t region) {
  if (region >= mpu_region_count) {
#ifdef POK_NEEDS_DEBUG
    printf("ERROR: Cannot disable MPU region %u (exceeds count %u)\n", region,
           mpu_region_count);
#endif
    return POK_ERRNO_EINVAL;
  }

  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  /* Select region and disable it */
  MPU_RNR = region;
  MPU_RASR &= ~MPU_RASR_ENABLE;

  /* Data and instruction synchronization barriers */
  __asm volatile("dsb" : : : "memory");
  __asm volatile("isb" : : : "memory");

  /* Update tracking - mark region disabled but preserve configuration */
  mpu_regions[region].enabled = 0;

  __set_PRIMASK(primask);

  return POK_ERRNO_OK;
}

pok_ret_t pok_mpu_enable(void) {
  /* Enable MPU with background region for privileged access */
  MPU_CTRL = MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA;

  /* Data Synchronization Barrier */
  __asm volatile("dsb" : : : "memory");
  /* Instruction Synchronization Barrier */
  __asm volatile("isb" : : : "memory");

  return POK_ERRNO_OK;
}

pok_ret_t pok_mpu_disable(void) {
  /* Disable MPU */
  MPU_CTRL = 0;

  __asm volatile("dsb" : : : "memory");
  __asm volatile("isb" : : : "memory");

  return POK_ERRNO_OK;
}

uint8_t pok_mpu_get_region_count(void) { return mpu_region_count; }

uint32_t pok_mpu_size_to_rasr(uint32_t size) {
  uint32_t rasr_size = 0;

  /* Size must be power of 2 and >= minimum size */
  if (size < MPU_MIN_REGION_SIZE || !mpu_is_power_of_2(size)) {
    return 0; /* Invalid size */
  }

  /* Calculate size field (log2(size) - 1) optimized using GCC builtin */
  /* For ARM Cortex-M, GCC will generate CLZ instruction when available */
  rasr_size = (31 - __builtin_clz(size)) - 1;

  return (rasr_size << MPU_RASR_SIZE_SHIFT) & MPU_RASR_SIZE_MASK;
}

uint8_t pok_mpu_get_active_user_region(void) {
  /* Iterate through user regions (1 to POK_CONFIG_NB_PARTITIONS) to find active
   * one */
  for (uint8_t region = 1; region <= POK_CONFIG_NB_PARTITIONS; region++) {
    if (region >= mpu_region_count) {
      break;
    }

    /* Select region to read its configuration */
    MPU_RNR = region;

    /* Check if region is enabled */
    if (MPU_RASR & MPU_RASR_ENABLE) {
      return region;
    }
  }

  return 0; /* No active user region found, return kernel region */
}

uint32_t pok_mpu_get_region_base(uint8_t region) {
  if (region >= mpu_region_count) {
    return 0;
  }

  /* Select region to read its configuration */
  MPU_RNR = region;

  /* Return base address (mask out region number and valid bit) */
  return (MPU_RBAR & ~(MPU_RBAR_REGION_MASK | MPU_RBAR_VALID));
}
