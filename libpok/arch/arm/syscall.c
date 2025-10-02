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
pok_ret_t pok_do_syscall(pok_syscall_id_t syscall_id,
                         pok_syscall_args_t *args) {
  pok_ret_t ret;
  uint32_t args_addr;
  uint32_t id;

  args_addr = (uint32_t)args;
  id = (uint32_t)syscall_id;

  /* ARM Cortex-M syscall using SVC instruction
   * r0 = syscall_id, r1 = args_addr
   * Result returned in r0
   */
  __asm volatile("mov r0, %1          \n\t" /* Load syscall ID into r0 */
                 "mov r1, %2          \n\t" /* Load args address into r1 */
                 "svc #0              \n\t" /* Trigger supervisor call */
                 "mov %0, r0          \n\t" /* Get result from r0 */
                 : "=r"(ret)                /* Output: ret variable gets r0 */
                 : "r"(id),
                   "r"(args_addr)       /* Inputs: syscall_id, args pointer */
                 : "r0", "r1", "memory" /* Clobbered: r0, r1, memory */
  );

  return ret;
}
