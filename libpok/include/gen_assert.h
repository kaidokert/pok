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
 * \file    libpok/include/gen_assert.h
 * \brief   Assertion macros for Ocarina-generated partition code
 * \author  POK team
 *
 * This header provides ASSERT_RET and ASSERT_RET_WITH_EXCEPTION macros
 * that are used by Ocarina code generator in partition initialization
 * and runtime code.
 */

#ifndef __POK_GEN_ASSERT_H__
#define __POK_GEN_ASSERT_H__

#include <errno.h>
#include <types.h>

#ifdef POK_NEEDS_DEBUG
#include <libc/stdio.h>
#endif

/**
 * ASSERT_RET - Check POK return values in generated code
 *
 * Used by Ocarina-generated code to verify POK syscall return values.
 * In debug builds, prints error information. In release builds, no-op.
 */
#ifndef ASSERT_RET
#ifdef POK_NEEDS_DEBUG
#define ASSERT_RET(ret)                                                        \
  do {                                                                         \
    pok_ret_t __ret = (ret);                                                   \
    if (__ret != POK_ERRNO_OK) {                                               \
      printf("[ASSERT] Error %d at %s:%d\n", __ret, __FILE__, __LINE__);       \
    }                                                                          \
  } while (0)
#else
#define ASSERT_RET(ret)                                                        \
  do {                                                                         \
    (void)(ret);                                                               \
  } while (0)
#endif
#endif /* ASSERT_RET */

/**
 * ASSERT_RET_WITH_EXCEPTION - Check return value with allowed exception
 *
 * Used by Ocarina-generated code to verify POK syscall return values
 * while allowing one specific error code (e.g., POK_ERRNO_EMPTY for
 * reading from empty sampling ports).
 */
#ifndef ASSERT_RET_WITH_EXCEPTION
#ifdef POK_NEEDS_DEBUG
#define ASSERT_RET_WITH_EXCEPTION(ret, exc)                                    \
  do {                                                                         \
    pok_ret_t __ret = (ret);                                                   \
    if (__ret != POK_ERRNO_OK && __ret != (exc)) {                             \
      printf("[ASSERT] Error %d at %s:%d\n", __ret, __FILE__, __LINE__);       \
    }                                                                          \
  } while (0)
#else
#define ASSERT_RET_WITH_EXCEPTION(ret, exc)                                    \
  do {                                                                         \
    (void)(ret);                                                               \
    (void)(exc);                                                               \
  } while (0)
#endif
#endif /* ASSERT_RET_WITH_EXCEPTION */

#endif /* __POK_GEN_ASSERT_H__ */
