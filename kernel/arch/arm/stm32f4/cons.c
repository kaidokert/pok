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
#include "clock_config.h"

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
#define GPIOA_MODER       (*((volatile uint32_t *)(GPIOA_BASE + 0x00)))  /* Mode register */
#define GPIOA_OTYPER      (*((volatile uint32_t *)(GPIOA_BASE + 0x04)))  /* Output type register */
#define GPIOA_OSPEEDR     (*((volatile uint32_t *)(GPIOA_BASE + 0x08)))  /* Output speed register */
#define GPIOA_PUPDR       (*((volatile uint32_t *)(GPIOA_BASE + 0x0C)))  /* Pull-up/pull-down register */
#define GPIOA_AFRL        (*((volatile uint32_t *)(GPIOA_BASE + 0x20)))  /* AF[7:0] */
#define GPIOA_AFRH        (*((volatile uint32_t *)(GPIOA_BASE + 0x24)))  /* AF[15:8] */

pok_ret_t pok_cons_init(void) {
  /* Enable GPIOA and USART1 clocks */
  RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
  
  /* Configure PA9 (TX) and PA10 (RX) as alternate function */
  GPIOA_MODER &= ~((3 << 18) | (3 << 20));  /* Clear mode bits for PA9, PA10 */
  GPIOA_MODER |= (2 << 18) | (2 << 20);     /* Set alternate function mode */
  
  /* Set alternate function 7 (USART) for PA9 and PA10 */
  /* PA9 = pin 9 (AFRH bit 4-7), PA10 = pin 10 (AFRH bit 8-11) */
  GPIOA_AFRH &= ~((0xF << 4) | (0xF << 8));  /* Clear AF bits in AFRH register */
  GPIOA_AFRH |= (7 << 4) | (7 << 8);         /* Set AF7 for PA9 and PA10 */
  
  /* Configure output type as push-pull (default, but explicit) */
  GPIOA_OTYPER &= ~((1 << 9) | (1 << 10));   /* PA9, PA10 push-pull output */
  
  /* Configure high speed for 115200 baud reliability */
  GPIOA_OSPEEDR &= ~((3 << 18) | (3 << 20)); /* Clear speed bits */
  GPIOA_OSPEEDR |= (3 << 18) | (3 << 20);    /* Set very high speed (100MHz) */
  
  /* Configure pull-up for TX, no pull for RX (typical UART config) */
  GPIOA_PUPDR &= ~((3 << 18) | (3 << 20));   /* Clear pull bits */
  GPIOA_PUPDR |= (1 << 20);                  /* PA10 (RX) pull-up, PA9 (TX) no pull */
  /* Configure USART1 baud rate */
  /* For oversampling by 16: BRR = (mantissa << 4) + fraction */
  /* BRR_value = f_CK / (16 * baud_rate) */
  uint32_t apb2_clock = APB2_FREQ_HZ;  /* Use correct 84MHz APB2 clock */
  uint32_t baud_rate = 115200;
  uint32_t brr_value = (apb2_clock + (8 * baud_rate)) / (16 * baud_rate);  /* Rounded division */
  
  USART1_BRR = brr_value;
  
  /* Enable USART, transmitter, and receiver */
  USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
  
  return POK_ERRNO_OK;
}

pok_ret_t pok_cons_write(const char *s, size_t length) {
  if (s == NULL) {
    return POK_ERRNO_EINVAL;
  }
  
  for (size_t i = 0; i < length; i++) {
    /* Wait for transmit data register to be empty */
    while (!(USART1_SR & USART_SR_TXE)) {
      /* Wait */
    }
    
    /* Send character */
    USART1_DR = (uint8_t)s[i];
  }
  
  return POK_ERRNO_OK;
}

pok_ret_t pok_cons_read(char *s, size_t length) {
  if (s == NULL) {
    return POK_ERRNO_EINVAL;
  }
  
  for (size_t i = 0; i < length; i++) {
    /* Wait for receive data register to have data */
    while (!(USART1_SR & USART_SR_RXNE)) {
      /* Wait */
    }
    
    /* Read character */
    s[i] = USART1_DR & 0xFF;
  }
  
  return POK_ERRNO_OK;
}
