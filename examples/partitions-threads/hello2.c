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

#include <libc/stdio.h>
#include <libc/stdlib.h>

void user_hello_part2() {
  int a;
  int b = 10;
  int c;
  uint8_t *alloc1;
  uint8_t *alloc2;

  for (c = 0; c < 65000; c++) {
    a = 0 + 1 * b;
    b = b + 1;

    b = b - 2;
    b = b + 1;
    a++;
  }

  /* NOTE: malloc/calloc/free disabled due to ARM/Thumb linker issues
   * TODO: Fix stdlib linking for ARM architecture */
  (void)alloc1; /* Suppress unused variable warning */
  (void)alloc2;
  printf("[PART2] Hello World (computation result: a=%d, b=%d)\n", a, b);
}
