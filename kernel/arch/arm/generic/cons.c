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

typedef unsigned int size_t;
typedef int pok_bool_t;

#if defined(POK_NEEDS_CONSOLE) || defined(POK_NEEDS_DEBUG) ||                  \
    defined(POK_NEEDS_INSTRUMENTATION) || defined(POK_NEEDS_COVERAGE_INFOS) || \
    defined(POK_NEEDS_USER_DEBUG)

static void write_stub(char c) { (void)c; }

pok_bool_t pok_cons_write(const char *s, size_t length) {
  (void)s;
  (void)length;
  return 0;
}

int pok_cons_init(void) {
  pok_print_init(write_stub, NULL);
  return 0;
}
#else
int pok_cons_init(void) { return 0; }
#endif
