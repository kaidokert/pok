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
 * \file    core/memcheck.h
 * \brief   Runtime memory overlap detection
 * \author  POK team
 *
 * Provides runtime assertions to detect memory overlaps between kernel,
 * partitions, and heap. These checks run during initialization to catch
 * configuration errors that would cause memory corruption.
 */

#ifndef __POK_MEMCHECK_H__
#define __POK_MEMCHECK_H__

#include <errno.h>
#include <types.h>

/**
 * \brief Register a memory region for overlap checking
 *
 * Called during initialization to register each memory region (kernel,
 * partitions, heap, stacks, etc). The memcheck subsystem will verify
 * that no regions overlap.
 *
 * \param name Human-readable name for this region (for error messages)
 * \param start Start address of the region
 * \param size Size of the region in bytes
 * \return POK_ERRNO_OK on success, error code if overlap detected
 */
pok_ret_t pok_memcheck_register_region(const char *name, uint32_t start,
                                       uint32_t size);

/**
 * \brief Verify that all registered regions are disjoint
 *
 * Called after all regions have been registered. Checks every pair of
 * regions for overlaps. If an overlap is found, prints error message
 * and triggers kernel panic.
 *
 * \return POK_ERRNO_OK if no overlaps, POK_ERRNO_EINVAL if overlap found
 */
pok_ret_t pok_memcheck_verify_layout(void);

/**
 * \brief Initialize the memory checking subsystem
 *
 * Must be called early in boot process before any region registration.
 */
void pok_memcheck_init(void);

#endif /* __POK_MEMCHECK_H__ */
