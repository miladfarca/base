// Copyright (c) 2025 miladfarca

#include "stm32f10x.h"
#include "timer.h"
#include "usart.h"
#include "usb.h"
#include "utils.h"

void main() {

  timer_init();
  usart1_init();

  dbg_print("main: ------ Booting ------\n");
  dbg_print("main: Base starting\n");

  usb_init();

  dbg_print("main: Delay 10 seconds\n");
  timer_delay_ms(10000);

  for (;;)
    ;
}
