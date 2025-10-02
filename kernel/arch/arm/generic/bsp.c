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

#include "cons.h"

#define POK_ERRNO_OK 0
typedef unsigned int size_t;
typedef unsigned int uintptr_t;

extern int printf(const char *format, ...);

int pok_bsp_init(void) {
  pok_cons_init();
  printf("POK BSP initialization complete\n");
  return POK_ERRNO_OK;
}

extern char _ebss[];

static char *heap_end = _ebss;

void *pok_bsp_mem_alloc(size_t sz) {
  char *res;
  res = (char *)(((uintptr_t)heap_end + 4095) & ~4095);
  heap_end = res + sz;
  return res;
}
