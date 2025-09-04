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
 * \file    arch/arm/stm32f4/cons.c
 * \author  POK team  
 * \brief   STM32F4 console implementation via USART
 */

#include <errno.h>
#include <libc.h>

/* STM32F4 USART1 registers */
#define USART1_BASE       0x40011000
#define USART1_SR         (*((volatile uint32_t *)(USART1_BASE + 0x00)))
#define USART1_DR         (*((volatile uint32_t *)(USART1_BASE + 0x04)))
#define USART1_BRR        (*((volatile uint32_t *)(USART1_BASE + 0x08)))
#define USART1_CR1        (*((volatile uint32_t *)(USART1_BASE + 0x0C)))
#define USART1_CR2        (*((volatile uint32_t *)(USART1_BASE + 0x10)))
#define USART1_CR3        (*((volatile uint32_t *)(USART1_BASE + 0x14)))

/* USART status register bits */
#define USART_SR_TXE      (1 << 7)  /* Transmit data register empty */
#define USART_SR_RXNE     (1 << 5)  /* Read data register not empty */

/* USART control register 1 bits */
#define USART_CR1_UE      (1 << 13) /* USART enable */
#define USART_CR1_TE      (1 << 3)  /* Transmitter enable */
#define USART_CR1_RE      (1 << 2)  /* Receiver enable */

/* RCC registers for clock control */
#define RCC_BASE          0x40023800
#define RCC_APB2ENR       (*((volatile uint32_t *)(RCC_BASE + 0x44)))
#define RCC_AHB1ENR       (*((volatile uint32_t *)(RCC_BASE + 0x30)))

#define RCC_APB2ENR_USART1EN  (1 << 4)
#define RCC_AHB1ENR_GPIOAEN   (1 << 0)

/* GPIO registers for USART pins */
#define GPIOA_BASE        0x40020000
#define GPIOA_MODER       (*((volatile uint32_t *)(GPIOA_BASE + 0x00)))
#define GPIOA_AFRL        (*((volatile uint32_t *)(GPIOA_BASE + 0x20)))

pok_ret_t pok_cons_init(void) {
  /* Enable GPIOA and USART1 clocks */
  RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
  
  /* Configure PA9 (TX) and PA10 (RX) as alternate function */
  GPIOA_MODER &= ~((3 << 18) | (3 << 20));  /* Clear mode bits */
  GPIOA_MODER |= (2 << 18) | (2 << 20);     /* Set alternate function mode */
  
  /* Set alternate function 7 (USART) for PA9 and PA10 */
  GPIOA_AFRL &= ~((0xF << 4) | (0xF << 8)); /* Clear AF bits */
  GPIOA_AFRL |= (7 << 4) | (7 << 8);        /* Set AF7 */
  
  /* Configure USART1 */
  /* TODO: Calculate baud rate dynamically based on actual system clock frequency */
  /* BRR = fck / (16 * baud_rate) for oversampling by 16 */
  USART1_BRR = 16000000 / (16 * 115200);
  
  /* Enable USART, transmitter, and receiver */
  USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
  
  return (POK_ERRNO_OK);
}

pok_ret_t pok_cons_write(const char *s, size_t length) {
  if (s == NULL) {
    return (POK_ERRNO_EINVAL);
  }
  
  for (size_t i = 0; i < length; i++) {
    /* Wait for transmit data register to be empty */
    while (!(USART1_SR & USART_SR_TXE)) {
      /* Wait */
    }
    
    /* Send character */
    USART1_DR = s[i];
  }
  
  return (POK_ERRNO_OK);
}

pok_ret_t pok_cons_read(char *s, size_t length) {
  if (s == NULL) {
    return (POK_ERRNO_EINVAL);
  }
  
  for (size_t i = 0; i < length; i++) {
    /* Wait for receive data register to have data */
    while (!(USART1_SR & USART_SR_RXNE)) {
      /* Wait */
    }
    
    /* Read character */
    s[i] = USART1_DR & 0xFF;
  }
  
  return (POK_ERRNO_OK);
}