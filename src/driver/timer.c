// Copyright (c) 2025 miladfarca

#include "timer.h"
#include "stm32f10x.h"

// Counter for milliseconds.
static volatile uint32_t ms_ticks = 0;

void SysTick_Handler(void) { ms_ticks++; }

void timer_delay_ms(uint32_t delay_ticks) {
  uint32_t current_ticks = ms_ticks;
  while ((ms_ticks - current_ticks) < delay_ticks)
    ;
}

void timer_init() {
  // Initialize SysTick.
  if (SysTick_Config(SystemCoreClock / 1000)) {
    while (1)
      ; // Capture error
  }
}
