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

#include <core/multiprocessing.h>

/* ARM Cortex-M is single-core, so multiprocessing is disabled */
uint8_t multiprocessing_system = 0;

uint8_t pok_get_proc_id(void) { return 0; /* Single processor system */ }
