// Copyright (c) 2025 miladfarca

#include "usart.h"
#include "stm32f10x.h"

void usart1_init() {
  // Enable clock for GPIOA and USART1
  RCC->APB2ENR |= (RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN);

  // Configure PA9 (Tx) as alternate function push-pull
  // Clear the bit field
  GPIOA->CRH &= ~(GPIO_CRH_CNF9 | GPIO_CRH_MODE9);
  GPIOA->CRH |= (GPIO_CRH_CNF9_1 | GPIO_CRH_MODE9_1);

  // Configure PA10 (Rx) as input floating
  GPIOA->CRH &= ~(GPIO_CRH_CNF10 | GPIO_CRH_MODE10);
  GPIOA->CRH |= GPIO_CRH_CNF10_0;

  // BRR = APBxClock / Baud
  // https://community.st.com/t5/stm32-mcus-products/what-is-written-to-the-usart-brr-register/td-p/125766
  USART1->BRR = 72000000 / 9600;
  // Enable USART, Tx and Rx
  USART1->CR1 = (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE);
}

void usart1_write(int ch) {
  // Wait until Tx buffer is empty
  while (!(USART1->SR & USART_SR_TXE)) {
  }
  USART1->DR = (ch & 0xFF);
}
