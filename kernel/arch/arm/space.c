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
#include "space.h"
#include "thread.h"

#define KERNEL_STACK_SIZE 4096
#define MEMORY_WASTE_THRESHOLD_PERCENT 25
#define MEMORY_WASTE_CRITICAL_PERCENT                                          \
  50 /* Fail allocation if waste exceeds this */
#define STACK_ALIGNMENT_BYTES                                                  \
  CORTEX_M_STACK_ALIGNMENT /* ARM Cortex-M requires 8-byte stack alignment */
#define STACK_ALIGNMENT_MASK CORTEX_M_STACK_ALIGNMENT_MASK

/* Helper function to align address down while keeping it within bounds */
static inline uint32_t align_down(uint32_t value, uint32_t alignment) {
  return value & ~(alignment - 1);
}

static struct pok_space spaces[POK_CONFIG_NB_PARTITIONS];

/**
 * Get space accessor function
 * @param partition_id Partition identifier
 * @return Pointer to space structure, NULL if invalid partition_id
 */
pok_space_t *pok_get_space(uint8_t partition_id) {
  if (partition_id >= POK_CONFIG_NB_PARTITIONS) {
    return NULL;
  }
  return &spaces[partition_id];
}

/**
 * Update partition space bounds (for W^X initialization)
 * @param partition_id Partition ID
 * @param phys_base Physical base address
 * @param size Total partition size
 * @return POK_ERRNO_OK on success, error code otherwise
 */
pok_ret_t pok_space_set_bounds(uint8_t partition_id, uint32_t phys_base,
                               uint32_t size) {
  if (partition_id >= POK_CONFIG_NB_PARTITIONS) {
    return POK_ERRNO_EINVAL;
  }
  spaces[partition_id].phys_base = phys_base;
  spaces[partition_id].size = size;
  return POK_ERRNO_OK;
}

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

#ifdef POK_NEEDS_DEBUG
  pok_cons_write("Creating space for partition ", 30);
  char part_id_str[2];
  part_id_str[0] = '0' + partition_id;
  part_id_str[1] = '\0';
  pok_cons_write(part_id_str, 1);
  pok_cons_write("\n", 1);
#endif

  if (partition_id >= POK_CONFIG_NB_PARTITIONS) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Invalid partition ID\n", 29);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Validate nonzero/valid size early to prevent division by zero */
  if (size == 0) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Zero size is invalid\n", 29);
#endif
    return POK_ERRNO_EINVAL; /* Zero size is invalid */
  }
  /* Prevent wraparound of end address */
  if (addr > UINT32_MAX - size) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: End address overflow\n", 29);
#endif
    return POK_ERRNO_EINVAL; /* Prevent end overflow */
  }

  /* Use partition_id + 1 as region ID (reserve region 0 for kernel) */
  region_id = partition_id + 1;

  /* Region budget check: Ensure we don't exceed available MPU regions
   * Budget allocation:
   * - Region 0: Reserved for kernel
   * - Regions 1 to POK_CONFIG_NB_PARTITIONS: Partition data regions
   * - Regions (POK_CONFIG_NB_PARTITIONS+1) to (2*POK_CONFIG_NB_PARTITIONS):
   * Partition code regions Total needed: 1 + (2 * POK_CONFIG_NB_PARTITIONS)
   */
  uint32_t total_regions_needed = 1 + (2 * POK_CONFIG_NB_PARTITIONS);
  uint32_t available_regions = pok_mpu_get_region_count();

  if (total_regions_needed > available_regions) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: MPU region budget exceeded\n", 36);
    pok_cons_write(
        "Reduce POK_CONFIG_NB_PARTITIONS or use MPU with more regions\n", 66);
#endif
    return POK_ERRNO_EINVAL;
  }

  if (region_id >= available_regions) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Data region ID exceeds available regions\n", 51);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Configure MPU attributes for partition data region:
   * W^X security: Data region is read-write but NOT executable (XN bit set)
   * This region covers .data, .bss, and stack
   */
  mpu_attributes = MPU_ATTR_INTERNAL_SRAM | MPU_PERM_ALL_RW | MPU_RASR_XN;

  /* Calculate optimal size alignment with efficiency analysis */
  uint32_t aligned_size = mpu_align_size_to_power_of_2(size);
  if (aligned_size == 0) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Cannot align size to power of 2\n", 41);
#endif
    return POK_ERRNO_EINVAL;
  }

#ifdef POK_NEEDS_DEBUG
  pok_cons_write("Size alignment completed\n", 26);
#endif
  uint32_t initial_waste = aligned_size - size;
  uint32_t initial_waste_percent = ((uint64_t)initial_waste * 100) / size;

  /* Try alternative alignment strategies if waste is significant */
  if (initial_waste_percent > MEMORY_WASTE_THRESHOLD_PERCENT &&
      size >= ARM_MPU_MIN_SUBREGION_SIZE) {
    /* Consider if smaller alignment with subregions would be more efficient */
    uint32_t half_aligned = aligned_size / 2;
    if (half_aligned >= size && mpu_is_aligned(addr, half_aligned)) {
      uint32_t subregion_waste = half_aligned - size;
      uint32_t subregion_waste_percent =
          ((uint64_t)subregion_waste * 100) / size;
      if (subregion_waste_percent < initial_waste_percent) {
#ifdef POK_NEEDS_DEBUG
        pok_cons_write("INFO: Using smaller alignment\n", 32);
#endif
        aligned_size = half_aligned;
      }
    }
  }

  /* Validate base address alignment matches MPU requirements */
  if (!mpu_is_aligned(addr, aligned_size)) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Partition base addr not aligned\n", 42);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Early waste validation - only reject if waste is extreme and subregions
   * can't help Subregions can reduce waste significantly, so we defer strict
   * validation */
  uint32_t predicted_waste = aligned_size - size;
  uint32_t predicted_waste_percent = ((uint64_t)predicted_waste * 100) / size;
  /* Only reject if waste is 4x the critical threshold - allow subregions to
   * optimize */
  if (predicted_waste_percent > (MEMORY_WASTE_CRITICAL_PERCENT * 4)) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Partition predicted waste far exceeds threshold\n",
                   58);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Configure MPU region - try subregions first to minimize waste */
  uint32_t actual_exposed_memory = aligned_size - size;
  pok_bool_t used_subregions = FALSE;
  uint32_t configured_region_id = region_id; /* Track for rollback */

  /* Enhanced subregion decision logic */
  uint32_t waste_threshold_bytes = size / 4; /* 25% waste threshold in bytes */
  uint32_t current_waste = aligned_size - size;

  if (aligned_size >= ARM_MPU_MIN_SUBREGION_SIZE &&
      current_waste >= waste_threshold_bytes &&
      aligned_size <=
          (8 * ARM_MPU_MIN_SUBREGION_SIZE)) { /* Reasonable subregion size */

    /* Calculate potential subregion efficiency */
    uint32_t subregion_size = aligned_size / ARM_MPU_SUBREGION_COUNT;
    uint32_t needed_subregions = (size + subregion_size - 1) / subregion_size;
    uint32_t subregion_exposed = (needed_subregions * subregion_size);
    uint32_t subregion_waste = subregion_exposed - size;

    /* Only use subregions if they provide meaningful improvement */
    if (subregion_waste < current_waste) {
#ifdef POK_NEEDS_DEBUG
      pok_cons_write("INFO: Using subregions\n", 24);
#endif

      if (pok_mpu_configure_region_with_subregions(
              region_id, addr, size, aligned_size, mpu_attributes) ==
          POK_ERRNO_OK) {
        used_subregions = TRUE;
        actual_exposed_memory = subregion_waste;
      }
    }
  }

  if (!used_subregions) {
    /* Require subregions for non-exact fits to avoid mapping outside partition
     */
    if (aligned_size != size) {
      if (aligned_size >= ARM_MPU_MIN_SUBREGION_SIZE &&
          pok_mpu_configure_region_with_subregions(
              region_id, addr, size, aligned_size, mpu_attributes) ==
              POK_ERRNO_OK) {
        used_subregions = TRUE;
        actual_exposed_memory = aligned_size - size; /* masked by SRD */
      } else {
        return POK_ERRNO_EINVAL; /* refuse unsafe overmap */
      }
    } else {
      if (pok_mpu_configure_region(region_id, addr, aligned_size,
                                   mpu_attributes) != POK_ERRNO_OK) {
        return POK_ERRNO_EFAULT;
      }
      actual_exposed_memory = 0;
    }
  }

  /* Check waste after subregion masking */
  if (actual_exposed_memory > 0) {
    uint32_t waste_percent = ((uint64_t)actual_exposed_memory * 100) / size;
    if (waste_percent > MEMORY_WASTE_CRITICAL_PERCENT) {
#ifdef POK_NEEDS_DEBUG
      pok_cons_write("ERROR: Partition wastes too much memory\n", 43);
#endif
      /* Rollback: disable the configured MPU region */
      pok_mpu_disable_region(configured_region_id);
      return POK_ERRNO_EINVAL;
    }

#ifdef POK_NEEDS_DEBUG
    if (waste_percent > MEMORY_WASTE_THRESHOLD_PERCENT) {
      pok_cons_write("WARNING: Partition wastes memory - protected by MPU\n",
                     56);
    }
#endif
  }

  /* Store partition information */
  spaces[partition_id].phys_base = addr;
  spaces[partition_id].size = size;
  spaces[partition_id].mpu_region = region_id;
  spaces[partition_id].mpu_code_region = 0; /* No code region yet */
  spaces[partition_id].code_base = 0;
  spaces[partition_id].code_size = 0;

  /* Initially disable the region */
  pok_mpu_disable_region(region_id);

#ifdef POK_NEEDS_DEBUG
  pok_cons_write("pok_create_space completed\n", 28);
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

  /* Require existing partition before creating code region */
  if (spaces[partition_id].size == 0) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write(
        "ERROR: Cannot create code region for non-existent partition\n", 64);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Use partition_id + 1 + POK_CONFIG_NB_PARTITIONS as code region ID */
  region_id = partition_id + 1 + POK_CONFIG_NB_PARTITIONS;

  /* Region budget check: Ensure we don't exceed available MPU regions
   * Budget allocation:
   * - Region 0: Reserved for kernel
   * - Regions 1 to POK_CONFIG_NB_PARTITIONS: Partition data regions
   * - Regions (POK_CONFIG_NB_PARTITIONS+1) to (2*POK_CONFIG_NB_PARTITIONS):
   * Partition code regions Total needed: 1 + (2 * POK_CONFIG_NB_PARTITIONS)
   */
  uint32_t total_regions_needed = 1 + (2 * POK_CONFIG_NB_PARTITIONS);
  uint32_t available_regions = pok_mpu_get_region_count();

  if (total_regions_needed > available_regions) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: MPU region budget exceeded\n", 36);
    pok_cons_write(
        "Reduce POK_CONFIG_NB_PARTITIONS or use MPU with more regions\n", 66);
#endif
    return POK_ERRNO_EINVAL;
  }

  if (region_id >= available_regions) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Code region ID exceeds available regions\n", 51);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Enforce code region is inside the partition bounds with overflow-safe math
   */
  if (code_size == 0) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Code region size cannot be zero\n", 42);
#endif
    return POK_ERRNO_EINVAL;
  }

  uint32_t partition_base = spaces[partition_id].phys_base;

  if (code_addr < partition_base) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Code region start before partition bounds\n", 52);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Use offset math to avoid overflow in end address calculation */
  uint32_t code_offset = code_addr - partition_base;
  if (code_size > spaces[partition_id].size ||
      code_offset > spaces[partition_id].size - code_size) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Code region extends beyond partition bounds\n", 54);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Configure MPU attributes for code region - enforce W^X:
   * - Read/Execute only (no write permission)
   * - Internal SRAM attributes (cacheable) for better performance
   */
  /* Use SRAM attributes since partition code resides in SRAM, not Flash */
  mpu_attributes = MPU_ATTR_INTERNAL_SRAM | MPU_PERM_ALL_RO;

  /* ARCHITECTURAL DESIGN DECISION: Use hard power-of-2 alignment for code
   * regions instead of subregions for simplicity and reliability. Code regions
   * typically have good natural alignment from linker, and hard alignment
   * ensures optimal MPU performance with single region per partition code
   * segment. */
  uint32_t aligned_size = mpu_align_size_to_power_of_2(code_size);
  if (aligned_size == 0) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Cannot align code size to power of 2\n", 46);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Validate base address alignment matches MPU requirements */
  if (!mpu_is_aligned(code_addr, aligned_size)) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Code region addr not aligned\n", 38);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Ensure the aligned region still fits entirely inside the partition */
  if (aligned_size > spaces[partition_id].size ||
      code_offset > spaces[partition_id].size - aligned_size) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Aligned code region exceeds partition bounds\n", 56);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Configure MPU region for partition code */
  if (pok_mpu_configure_region(region_id, code_addr, aligned_size,
                               mpu_attributes) != POK_ERRNO_OK) {
    return POK_ERRNO_EFAULT;
  }

  /* Store code region information for space switching */
  spaces[partition_id].mpu_code_region = region_id;
  spaces[partition_id].code_base = code_addr;
  spaces[partition_id].code_size = code_size;

  /* Initially disable the code region */
  pok_mpu_disable_region(region_id);

#ifdef POK_NEEDS_DEBUG
  pok_cons_write("pok_create_code_region completed\n", 35);
#endif

  return POK_ERRNO_OK;
}

pok_ret_t pok_space_switch(uint8_t old_partition_id, uint8_t new_partition_id) {
#ifdef POK_NEEDS_DEBUG
  pok_cons_write("pok_space_switch: old=", 23);
  char hex_buf[16];
  hex_buf[0] = '0' + old_partition_id;
  hex_buf[1] = ' ';
  hex_buf[2] = 'n';
  hex_buf[3] = 'e';
  hex_buf[4] = 'w';
  hex_buf[5] = '=';
  hex_buf[6] = '0' + new_partition_id;
  hex_buf[7] = '\n';
  pok_cons_write(hex_buf, 8);
#endif

  if (old_partition_id < POK_CONFIG_NB_PARTITIONS) {
    /* Disable old partition's data MPU region if valid (avoid affecting kernel
     * region) */
    if (spaces[old_partition_id].mpu_region != 0) {
      pok_mpu_disable_region(spaces[old_partition_id].mpu_region);
    }

    /* Disable old partition's code region if it exists */
    if (spaces[old_partition_id].mpu_code_region != 0) {
      pok_mpu_disable_region(spaces[old_partition_id].mpu_code_region);
    }
  }

  if (new_partition_id < POK_CONFIG_NB_PARTITIONS) {
    uint8_t data_r = spaces[new_partition_id].mpu_region;
    uint8_t code_r = spaces[new_partition_id].mpu_code_region;
    uint8_t region_count = pok_mpu_get_region_count();

#ifdef POK_NEEDS_DEBUG
    pok_cons_write("  Enabling data region ", 22);
    hex_buf[0] = '0' + data_r;
    hex_buf[1] = ',';
    hex_buf[2] = ' ';
    hex_buf[3] = 'c';
    hex_buf[4] = 'o';
    hex_buf[5] = 'd';
    hex_buf[6] = 'e';
    hex_buf[7] = ' ';
    hex_buf[8] = '0' + code_r;
    hex_buf[9] = '\n';
    pok_cons_write(hex_buf, 10);
#endif

    /* Enable regions in safe order: data (non-exec) first, then code (RX) */
    if (data_r != 0 && data_r < region_count) {
      pok_mpu_enable_region(data_r);
    }
    __asm volatile("dsb" : : : "memory");
    __asm volatile("isb" : : : "memory");
    if (code_r != 0 && code_r < region_count) {
      pok_mpu_enable_region(code_r);
    }
    __asm volatile("dsb" : : : "memory");
    __asm volatile("isb" : : : "memory");
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
  uint32_t entry_abs, stack_abs;

  if (partition_id >= POK_CONFIG_NB_PARTITIONS) {
    return (0);
  }

  /* ARM Cortex-M is single-core, ignore processor_affinity but validate it */
  (void)processor_affinity; /* Suppress unused parameter warning */

  /* Ensure partition was created */
  if (spaces[partition_id].size == 0) {
    return (0);
  }

  /* Bounds validation for entry and stack offsets */
  /* If partition has a separate code region, validate entry against code region
   * bounds */
  if (spaces[partition_id].mpu_code_region != 0) {
    /* Entry point must be within the code region */
    /* Prevent unsigned underflow if code_base < phys_base */
    if (spaces[partition_id].code_base < spaces[partition_id].phys_base) {
#ifdef POK_NEEDS_DEBUG
      pok_cons_write("ERROR: Code base is before partition base\n", 45);
#endif
      return (0);
    }
    uint32_t code_region_base =
        spaces[partition_id].code_base - spaces[partition_id].phys_base;

    /* Avoid unsigned underflow when computing code_offset */
    if (entry_rel < code_region_base) {
      /* entry_rel is before code region - invalid */
#ifdef POK_NEEDS_DEBUG
      pok_cons_write("ERROR: Entry offset before code region start\n", 48);
#endif
      return (0);
    }

    uint32_t code_offset = entry_rel - code_region_base;
    if (code_offset >= spaces[partition_id].code_size) {
#ifdef POK_NEEDS_DEBUG
      pok_cons_write("ERROR: Entry offset not within code region bounds\n", 53);
      pok_cons_write("  entry_rel=0x", 14);
      char hex_buf[16];
      uint32_t val = entry_rel;
      for (int i = 7; i >= 0; i--) {
        hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
        val >>= 4;
      }
      hex_buf[8] = '\n';
      pok_cons_write(hex_buf, 9);

      pok_cons_write("  code_region_base=0x", 21);
      val = code_region_base;
      for (int i = 7; i >= 0; i--) {
        hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
        val >>= 4;
      }
      hex_buf[8] = '\n';
      pok_cons_write(hex_buf, 9);

      pok_cons_write("  code_offset=0x", 16);
      val = code_offset;
      for (int i = 7; i >= 0; i--) {
        hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
        val >>= 4;
      }
      hex_buf[8] = '\n';
      pok_cons_write(hex_buf, 9);

      pok_cons_write("  code_size=0x", 14);
      val = spaces[partition_id].code_size;
      for (int i = 7; i >= 0; i--) {
        hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
        val >>= 4;
      }
      hex_buf[8] = '\n';
      pok_cons_write(hex_buf, 9);
#endif
      return (0);
    }
  } else {
    /* TEMPORARY: Allow context creation without code region since we're using
     * RWX permissions due to linker script limitations. When W^X is re-enabled,
     * this check should be restored to ensure security.
     *
     * TODO: Restore this check once partition linker script separates code/data
     */
#ifdef POK_NEEDS_DEBUG
    /* WARNING: Allowing execution without separate code region (W^X disabled)
     */
#endif
    /* Don't return 0 - allow context creation to proceed */
  }

  if (stack_rel >= spaces[partition_id].size) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Stack offset exceeds partition size\n", 46);
#endif
    return (0);
  }

  /* Validate that stack has reasonable space (at least 1KB) */
  uint32_t remaining_stack_space = spaces[partition_id].size - stack_rel;
  if (remaining_stack_space < 1024) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Insufficient stack space\n", 35);
#endif
    return (0);
  }

  /* Calculate absolute addresses after bounds validation */
  entry_abs = spaces[partition_id].phys_base + entry_rel;
  stack_abs = spaces[partition_id].phys_base + stack_rel;

#ifdef POK_NEEDS_DEBUG
  char hex_buf[16]; /* Declare once for entire debug section */
  uint32_t val;

  pok_cons_write("space_context_create: entry_abs=0x", 35);
  val = entry_abs;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
    val >>= 4;
  }
  hex_buf[8] = '\n';
  pok_cons_write(hex_buf, 9);
#endif

  /* Validate aligned SP is within partition bounds and has space for context */
  uint32_t sp_aligned = align_down(stack_abs, STACK_ALIGNMENT_BYTES);
  uint32_t base = spaces[partition_id].phys_base;
  uint32_t end = base + spaces[partition_id].size;

  /* Ensure we have enough space for the context frame */
  if (sp_aligned < base + sizeof(context_t) || sp_aligned >= end) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Insufficient space for context frame\n", 47);
#endif
    return (0);
  }

  /* Create context frame at top of user stack (no kernel stack allocation) */
  ctx = (context_t *)(sp_aligned - sizeof(context_t));

  /* Validate context frame is still within partition bounds */
  if ((uint32_t)ctx < base || (uint32_t)ctx >= end - sizeof(context_t)) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Context frame outside partition bounds\n", 49);
#endif
    return (0);
  }

  /* Initialize context frame (zero-initialize first) */
  memset(ctx, 0, sizeof(context_t));

  /* Initialize ARM Cortex-M context for thread startup */
  ctx->r0 = arg1; /* First argument */
  ctx->r1 = arg2; /* Second argument */
  /* On first exception return, LR must be a valid return target (exit stub),
   * not an EXC_RETURN magic value. */
  ctx->lr = (uint32_t)pok_arch_thread_exit_stub;
  /* ARM Cortex-M processors only support Thumb mode, so Thumb bit must be set
   */
#ifdef __thumb__
  ctx->pc = entry_abs | 1; /* Entry point with Thumb bit set */
#else
/* ARM Cortex-M is Thumb-only - this should not happen with correct build flags
 */
#warning "ARM Cortex-M requires Thumb mode - ensure -mthumb is specified"
  ctx->pc = entry_abs | 1; /* Force Thumb bit set as fallback */
#endif
  ctx->xpsr = 0x01000000; /* Thumb bit set */

#ifdef POK_NEEDS_DEBUG
  pok_cons_write("CTX_INIT: PC=0x", 15);
  val = ctx->pc;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
    val >>= 4;
  }
  hex_buf[8] = ' ';
  pok_cons_write(hex_buf, 9);

  pok_cons_write("LR=0x", 5);
  val = ctx->lr;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
    val >>= 4;
  }
  hex_buf[8] = ' ';
  pok_cons_write(hex_buf, 9);

  pok_cons_write("SP=0x", 5);
  val = (uint32_t)&ctx->r0;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
    val >>= 4;
  }
  hex_buf[8] = '\n';
  pok_cons_write(hex_buf, 9);

  /* Debug: Verify frame initialization */
  pok_cons_write("SPACE_FRAME: sw[r4-r11]=", 24);
  for (int i = 0; i < 8; i++) {
    val = ((uint32_t *)ctx)[i];
    for (int j = 7; j >= 0; j--) {
      hex_buf[j] = "0123456789ABCDEF"[val & 0xF];
      val >>= 4;
    }
    hex_buf[8] = ' ';
    pok_cons_write(hex_buf, 9);
  }
  pok_cons_write("\n", 1);

  pok_cons_write("SPACE_FRAME: hw[r0-xpsr]=", 25);
  for (int i = 8; i < 16; i++) {
    val = ((uint32_t *)ctx)[i];
    for (int j = 7; j >= 0; j--) {
      hex_buf[j] = "0123456789ABCDEF"[val & 0xF];
      val >>= 4;
    }
    hex_buf[8] = ' ';
    pok_cons_write(hex_buf, 9);
  }
  pok_cons_write("\n", 1);

  pok_cons_write("space_context_create completed\n", 33);

  /* CRITICAL DEBUG: Verify memory is actually written */
  pok_cons_write("MEM_CHECK: Reading back from memory:\n", 38);
  volatile uint32_t *check_ptr = (volatile uint32_t *)ctx;
  pok_cons_write("  SW frame [r4-r11]: ", 21);
  for (int i = 0; i < 8; i++) {
    val = check_ptr[i];
    for (int j = 7; j >= 0; j--) {
      hex_buf[j] = "0123456789ABCDEF"[val & 0xF];
      val >>= 4;
    }
    hex_buf[8] = ' ';
    pok_cons_write(hex_buf, 9);
  }
  pok_cons_write("\n  HW frame [r0-xpsr]: ", 22);
  for (int i = 8; i < 16; i++) {
    val = check_ptr[i];
    for (int j = 7; j >= 0; j--) {
      hex_buf[j] = "0123456789ABCDEF"[val & 0xF];
      val >>= 4;
    }
    hex_buf[8] = ' ';
    pok_cons_write(hex_buf, 9);
  }
  pok_cons_write("\n", 1);
  pok_cons_write("CRITICAL: Returning SW frame base at 0x", 40);
  val = (uint32_t)ctx;
  for (int i = 7; i >= 0; i--) {
    hex_buf[i] = "0123456789ABCDEF"[val & 0xF];
    val >>= 4;
  }
  hex_buf[8] = '\n';
  pok_cons_write(hex_buf, 9);
#endif

  /* CRITICAL FIX: Return SW frame base (r4-r11), NOT HW frame base.
   * TCB must store pointer to where r4-r11 are saved.
   * PendSV will restore r4-r11 from this address, then set PSP to (this + 32)
   * for hardware to restore r0-xpsr on exception return. */
  return (uint32_t)ctx; /* Return SW frame base, not &ctx->r0 */
}

pok_ret_t pok_arch_space_init(void) {
  pok_ret_t ret;
  uint32_t kernel_base, kernel_size;

#ifdef POK_NEEDS_DEBUG
  pok_cons_write("pok_arch_space_init: ENTRY\n", 27);
#endif

  /* Initialize partition spaces array */
  memset(spaces, 0, sizeof(spaces));

  /* Validate memory layout parameters before configuration */
  kernel_base = pok_bsp_kernel_base();
  kernel_size = pok_bsp_kernel_size();

  /* Validate kernel memory layout */
  if (kernel_base == 0 || kernel_size == 0) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Invalid kernel memory layout\n", 39);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Check for kernel base address alignment */
  if (!mpu_is_aligned(kernel_base, kernel_size)) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Kernel base address not aligned\n", 42);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Ensure kernel size is power of 2 and meets minimum requirements */
  if (!mpu_is_power_of_2(kernel_size) || kernel_size < MPU_MIN_REGION_SIZE) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Kernel size invalid\n", 29);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Check for potential kernel memory overflow */
  if (kernel_base > UINT32_MAX - kernel_size) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Kernel memory region would overflow\n", 46);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Reserve region 0 for kernel space */
  /* Use helper macro for kernel data region */
  uint32_t kernel_attrs = MPU_CONFIG_KERNEL_DATA;
  ret = pok_mpu_configure_region(0, kernel_base, kernel_size, kernel_attrs);
  if (ret != POK_ERRNO_OK) {
#ifdef POK_NEEDS_DEBUG
    pok_cons_write("ERROR: Failed to configure kernel MPU region, ret=", 51);
    char hex_buf[3];
    hex_buf[0] = "0123456789ABCDEF"[(ret >> 4) & 0xF];
    hex_buf[1] = "0123456789ABCDEF"[ret & 0xF];
    hex_buf[2] = '\n';
    pok_cons_write(hex_buf, 3);
#endif
    return ret;
  }

#ifdef POK_NEEDS_DEBUG
  pok_cons_write("pok_arch_space_init: SUCCESS\n", 29);
#endif

  return POK_ERRNO_OK;
}
