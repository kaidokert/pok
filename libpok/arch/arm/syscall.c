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
 * \file libpok/arch/arm/syscall.c
 * \brief ARM Cortex-M system call interface implementation
 * \author POK team
 */

#include <core/syscall.h>
#include <types.h>

/**
 * \brief Execute a system call using ARM SVC instruction
 *
 * This function performs the low-level interface between partition code
 * and the POK kernel using the ARM Cortex-M Supervisor Call (SVC) instruction.
 *
 * The ARM syscall convention used by POK:
 * - r0: syscall_id (input/output - returns result)
 * - r1: args pointer (input)
 * - SVC #0: trigger system call
 *
 * The kernel's SVC_Handler extracts these arguments from the exception frame
 * and calls pok_core_syscall() to handle the request.
 *
 * \param syscall_id The system call identifier
 * \param args Pointer to system call arguments structure
 * \return System call return value (POK_ERRNO_OK on success, error code
 * otherwise)
 */
__attribute__((noinline, noclone)) pok_ret_t
pok_do_syscall(pok_syscall_id_t syscall_id, pok_syscall_args_t *args) {
  /* ARM Cortex-M syscall using SVC instruction
   * r0 = syscall_id (input/output)
   * r1 = args pointer (input)
   *
   * NOTE: Using "0" constraint to force r0 reuse for input/output
   */
  register pok_ret_t ret __asm("r0") = syscall_id;
  register pok_syscall_args_t *r1 __asm("r1") = args;

  __asm volatile("dsb                 \n\t" /* Memory barrier before SVC */
                 "isb                 \n\t" /* Instruction barrier before SVC */
                 "svc #0              \n\t" /* Trigger supervisor call */
                 : "+r"(ret)                /* Input/output: r0 */
                 : "r"(r1)                  /* Input: r1 = args */
                 : "memory"                 /* Memory clobbered by kernel */
  );

  return ret;
}
