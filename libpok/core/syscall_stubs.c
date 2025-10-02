/*
 * POK syscall function implementations for ARM
 * These implement the pok_syscall0-5 functions that call pok_do_syscall
 */

#include <core/syscall.h>

pok_ret_t pok_syscall0(pok_syscall_id_t syscall_id) {
  return pok_do_syscall(syscall_id, &((pok_syscall_args_t){0, 0, 0, 0, 0, 0}));
}

pok_ret_t pok_syscall1(pok_syscall_id_t syscall_id, uint32_t arg1) {
  return pok_do_syscall(syscall_id,
                        &((pok_syscall_args_t){1, arg1, 0, 0, 0, 0}));
}

pok_ret_t pok_syscall2(pok_syscall_id_t syscall_id, uint32_t arg1,
                       uint32_t arg2) {
  return pok_do_syscall(syscall_id,
                        &((pok_syscall_args_t){2, arg1, arg2, 0, 0, 0}));
}

pok_ret_t pok_syscall3(pok_syscall_id_t syscall_id, uint32_t arg1,
                       uint32_t arg2, uint32_t arg3) {
  return pok_do_syscall(syscall_id,
                        &((pok_syscall_args_t){3, arg1, arg2, arg3, 0, 0}));
}

pok_ret_t pok_syscall4(pok_syscall_id_t syscall_id, uint32_t arg1,
                       uint32_t arg2, uint32_t arg3, uint32_t arg4) {
  return pok_do_syscall(syscall_id,
                        &((pok_syscall_args_t){4, arg1, arg2, arg3, arg4, 0}));
}

pok_ret_t pok_syscall5(pok_syscall_id_t syscall_id, uint32_t arg1,
                       uint32_t arg2, uint32_t arg3, uint32_t arg4,
                       uint32_t arg5) {
  return pok_do_syscall(
      syscall_id, &((pok_syscall_args_t){5, arg1, arg2, arg3, arg4, arg5}));
}
