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

#ifndef __POK_ARM_IPI_H__
#define __POK_ARM_IPI_H__

/* ARM Cortex-M is single-core, so IPI operations are stubs */
void pok_end_ipi(void);

#endif /* __POK_ARM_IPI_H__ */
