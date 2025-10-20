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
 * \file    core/memcheck.c
 * \brief   Runtime memory overlap detection implementation
 * \author  POK team
 */

#include <core/debug.h>
#include <core/error.h>
#include <core/memcheck.h>
#include <libc.h>

#define POK_MEMCHECK_MAX_REGIONS 32

typedef struct {
  const char *name;
  uint32_t start;
  uint32_t size;
  uint32_t end; /* start + size */
} pok_memregion_t;

static pok_memregion_t regions[POK_MEMCHECK_MAX_REGIONS];
static uint8_t num_regions = 0;
static bool_t initialized = FALSE;

void pok_memcheck_init(void) {
  num_regions = 0;
  initialized = TRUE;
}

pok_ret_t pok_memcheck_register_region(const char *name, uint32_t start,
                                       uint32_t size) {
  if (!initialized) {
    pok_memcheck_init();
  }

  if (num_regions >= POK_MEMCHECK_MAX_REGIONS) {
#ifdef POK_NEEDS_DEBUG
    printf("[MEMCHECK] ERROR: Too many regions (max %d)\n",
           POK_MEMCHECK_MAX_REGIONS);
#endif
    return POK_ERRNO_EINVAL;
  }

  /* Zero-size regions are ignored */
  if (size == 0) {
    return POK_ERRNO_OK;
  }

  regions[num_regions].name = name;
  regions[num_regions].start = start;
  regions[num_regions].size = size;
  regions[num_regions].end = start + size;

#ifdef POK_NEEDS_DEBUG
  printf("[MEMCHECK] Registered: %s @ 0x%x - 0x%x (size: 0x%x)\n", name, start,
         start + size, size);
#endif

  num_regions++;
  return POK_ERRNO_OK;
}

/**
 * \brief Check if two memory regions overlap
 */
static bool_t regions_overlap(const pok_memregion_t *r1,
                              const pok_memregion_t *r2) {
  /* Regions overlap if NOT (r1 ends before r2 starts OR r2 ends before r1
   * starts) */
  return !(r1->end <= r2->start || r2->end <= r1->start);
}

pok_ret_t pok_memcheck_verify_layout(void) {
  uint8_t i, j;
  bool_t overlap_found = FALSE;

#ifdef POK_NEEDS_DEBUG
  printf("\n");
  printf("======================================\n");
  printf("Memory Layout Verification\n");
  printf("======================================\n");
  printf("Checking %d regions for overlaps...\n", num_regions);
  printf("\n");
#endif

  /* Check all pairs of regions for overlaps */
  for (i = 0; i < num_regions; i++) {
    for (j = i + 1; j < num_regions; j++) {
      if (regions_overlap(&regions[i], &regions[j])) {
        overlap_found = TRUE;

        /* Print error message */
        printf("\n");
        printf("*** MEMORY OVERLAP DETECTED ***\n");
        printf("Region '%s' overlaps with '%s'\n", regions[i].name,
               regions[j].name);
        printf("  %s: 0x%x - 0x%x (size: 0x%x)\n", regions[i].name,
               regions[i].start, regions[i].end, regions[i].size);
        printf("  %s: 0x%x - 0x%x (size: 0x%x)\n", regions[j].name,
               regions[j].start, regions[j].end, regions[j].size);

        /* Calculate overlap region */
        uint32_t overlap_start = (regions[i].start > regions[j].start)
                                     ? regions[i].start
                                     : regions[j].start;
        uint32_t overlap_end =
            (regions[i].end < regions[j].end) ? regions[i].end : regions[j].end;
        uint32_t overlap_size = overlap_end - overlap_start;

        printf("  Overlap: 0x%x - 0x%x (%d bytes)\n", overlap_start,
               overlap_end, overlap_size);
        printf("\n");
      }
    }
  }

  if (overlap_found) {
    printf("======================================\n");
    printf("FATAL: Memory layout is invalid!\n");
    printf("Fix configuration before continuing.\n");
    printf("======================================\n");
    printf("\n");

    /* Trigger kernel panic */
    pok_kernel_error(POK_ERROR_KIND_KERNEL_CONFIG);
    return POK_ERRNO_EINVAL;
  }

#ifdef POK_NEEDS_DEBUG
  printf("✓ No overlaps detected\n");
  printf("✓ Memory layout is valid\n");
  printf("======================================\n");
  printf("\n");
#endif

  return POK_ERRNO_OK;
}
