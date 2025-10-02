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
typedef volatile unsigned int uint32_t;

#if defined(POK_NEEDS_CONSOLE) || defined(POK_NEEDS_DEBUG) ||                  \
    defined(POK_NEEDS_INSTRUMENTATION) || defined(POK_NEEDS_COVERAGE_INFOS) || \
    defined(POK_NEEDS_USER_DEBUG)

/* STM32F4 USART1 registers - mapped for QEMU console */
#define USART1_BASE 0x40011000
#define USART_SR (*(uint32_t *)(USART1_BASE + 0x00))  /* Status register */
#define USART_DR (*(uint32_t *)(USART1_BASE + 0x04))  /* Data register */
#define USART_BRR (*(uint32_t *)(USART1_BASE + 0x08)) /* Baud rate register */
#define USART_CR1 (*(uint32_t *)(USART1_BASE + 0x0C)) /* Control register 1 */

/* USART_SR bits */
#define USART_SR_TXE (1 << 7) /* Transmit data register empty */
#define USART_SR_TC (1 << 6)  /* Transmission complete */

/* USART_CR1 bits */
#define USART_CR1_UE (1 << 13) /* USART enable */
#define USART_CR1_TE (1 << 3)  /* Transmitter enable */
#define USART_CR1_RE (1 << 2)  /* Receiver enable */

/* Simple STM32F4 UART write function - try multiple UART addresses */
static void stm32f4_uart_putchar(char c) {
  /* Try multiple UART bases that might work in QEMU */
  volatile uint32_t *uart_bases[] = {
      (uint32_t *)0x40011000, /* USART1 */
      (uint32_t *)0x40004400, /* USART2 */
      (uint32_t *)0x40004800, /* USART3 */
      (uint32_t *)0x40013800, /* UART4 */
  };

  /* Write to all UARTs - one of them should work for QEMU console */
  for (int i = 0; i < 4; i++) {
    uart_bases[i][1] = (uint32_t)c; /* Offset 0x04 = data register */
  }

  /* Small delay */
  for (volatile int j = 0; j < 1000; j++) {
    /* delay */
  }
}

/* Initialize STM32F4 UART1 for console output */
static void stm32f4_uart_init(void) {
  /* Basic UART initialization for QEMU */
  /* Note: QEMU doesn't require full clock/GPIO setup like real hardware */

  /* Set baud rate to 115200 (simplified calculation for QEMU) */
  USART_BRR = 0x8B; /* 16MHz / 115200 ≈ 139 (0x8B) */

  /* Enable UART, transmitter, and receiver */
  USART_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

pok_bool_t pok_cons_write(const char *s, size_t length) {
  for (size_t i = 0; i < length; i++) {
    stm32f4_uart_putchar(s[i]);
    /* Add carriage return before newline for proper console output */
    if (s[i] == '\n') {
      stm32f4_uart_putchar('\r');
    }
  }
  return 1; /* Success */
}

int pok_cons_init(void) {
  stm32f4_uart_init();

  /* Test immediate output to verify UART works */
  const char *test_msg = "POK UART Console Initialized\n\r";
  while (*test_msg) {
    stm32f4_uart_putchar(*test_msg++);
  }

  pok_print_init(stm32f4_uart_putchar, NULL);
  return 0;
}
#else
int pok_cons_init(void) { return 0; }
#endif
