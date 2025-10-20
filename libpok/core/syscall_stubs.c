/*
 * POK syscall function implementations for ARM
 * These implement the pok_syscall0-5 functions that call pok_do_syscall
 *
 * CRITICAL: All syscall wrappers must be noinline to prevent compiler from
 * optimizing away the stack frame setup and register assignments.
 */

#include <core/syscall.h>

__attribute__((noinline)) pok_ret_t pok_syscall0(pok_syscall_id_t syscall_id) {
  volatile pok_syscall_id_t saved_id = syscall_id;
  pok_syscall_args_t args = {0, 0, 0, 0, 0, 0};
  return pok_do_syscall(saved_id, &args);
}

__attribute__((noinline)) pok_ret_t pok_syscall1(pok_syscall_id_t syscall_id,
                                                 uint32_t arg1) {
  volatile pok_syscall_id_t saved_id = syscall_id;
  pok_syscall_args_t args = {1, arg1, 0, 0, 0, 0};
  return pok_do_syscall(saved_id, &args);
}

__attribute__((noinline)) pok_ret_t pok_syscall2(pok_syscall_id_t syscall_id,
                                                 uint32_t arg1, uint32_t arg2) {
  volatile pok_syscall_id_t saved_id = syscall_id;
  pok_syscall_args_t args = {2, arg1, arg2, 0, 0, 0};
  return pok_do_syscall(saved_id, &args);
}

__attribute__((noinline)) pok_ret_t pok_syscall3(pok_syscall_id_t syscall_id,
                                                 uint32_t arg1, uint32_t arg2,
                                                 uint32_t arg3) {
  volatile pok_syscall_id_t saved_id = syscall_id;
  pok_syscall_args_t args = {3, arg1, arg2, arg3, 0, 0};
  return pok_do_syscall(saved_id, &args);
}

__attribute__((noinline)) pok_ret_t pok_syscall4(pok_syscall_id_t syscall_id,
                                                 uint32_t arg1, uint32_t arg2,
                                                 uint32_t arg3, uint32_t arg4) {
  volatile pok_syscall_id_t saved_id = syscall_id;
  pok_syscall_args_t args = {4, arg1, arg2, arg3, arg4, 0};
  return pok_do_syscall(saved_id, &args);
}

__attribute__((noinline)) pok_ret_t pok_syscall5(pok_syscall_id_t syscall_id,
                                                 uint32_t arg1, uint32_t arg2,
                                                 uint32_t arg3, uint32_t arg4,
                                                 uint32_t arg5) {
  /* CRITICAL: Save syscall_id to a local variable BEFORE constructing args.
   * The compiler will allocate saved_id to a callee-saved register (r4-r11)
   * which won't be clobbered during the args initialization. */
  volatile pok_syscall_id_t saved_id = syscall_id;
  pok_syscall_args_t args = {5, arg1, arg2, arg3, arg4, arg5};
  return pok_do_syscall(saved_id, &args);
}
