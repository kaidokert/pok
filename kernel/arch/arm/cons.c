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
 * \file    arch/arm/cons.c
 * \brief   ARM generic console placeholder
 * \author  POK team
 *
 * This file is intentionally minimal as ARM console implementations are
 * Board Support Package (BSP) specific. Each ARM BSP provides its own
 * console implementation in arch/arm/<bsp>/cons.c based on the specific
 * UART/USART hardware available on that platform.
 *
 * For example:
 * - STM32F4 uses USART1 (arch/arm/stm32f4/cons.c)
 * - Other ARM platforms would implement their specific UART drivers
 *
 * The BSP-specific console implementation must provide:
 * - pok_cons_init(): Initialize UART/console hardware
 * - pok_cons_write(): Write data to console output
 * - pok_cons_read(): Read data from console input (if supported)
 *
 * This approach allows POK to support diverse ARM microcontrollers and
 * development boards while maintaining a consistent console interface.
 */

#include <bsp.h>
#include <errno.h>
#include <types.h>

/* Weak stub implementations - BSP-specific implementations can override these
 */

__attribute__((weak)) pok_ret_t pok_cons_init(void) {
  /* Default stub - BSP should override this */
  return POK_ERRNO_OK;
}

__attribute__((weak)) pok_ret_t pok_cons_write(const char *s, size_t length) {
  /* Default stub - BSP should override this */
  (void)s;                 /* Suppress unused parameter warning */
  (void)length;            /* Suppress unused parameter warning */
  return POK_ERRNO_EFAULT; /* Indicate write failed/not supported */
}

__attribute__((weak)) pok_ret_t pok_cons_read(char *s, size_t length) {
  /* Default stub - BSP should override this */
  (void)s;                      /* Suppress unused parameter warning */
  (void)length;                 /* Suppress unused parameter warning */
  return POK_ERRNO_UNAVAILABLE; /* Indicate read not supported */
}

__attribute__((weak)) void pok_cons_get_char(char *c) {
  /* Default stub - BSP should override this */
  if (c) {
    *c = '\0'; /* Return null character if no input available */
  }
}
