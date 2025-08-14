// Copyright (c) 2025 miladfarca

enum usb_state { BOOT, INIT, CONFIGURED };

enum uart_state {
  DISABLED,
  ENABLED,
};

void usb_init();
void usb_pend_address(int addr);
void usb_endpoint_send(int ep, char *buf, int count);
void usb_endpoint_send_zlp(int ep);
int usb_endpoint_recv(int ep, char *buf);
void usb_terminal_print(char *string);
void USB_LP_CAN1_RX0_IRQHandler();
