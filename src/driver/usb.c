/*
 * Forked from https://github.com/trebisky/stm32f103/blob/master/usb/usb.c
 * Changes by: miladfarca, 2025
 *
 * This file was forked from the above repository and includes the following
 * changes:
 *   - Rewritten to use Arm CMSIS library
 *   - Made data‑wait on endpoint non‑blocking
 *   - Performed a large‑scale code cleanup and refactor
 *
 * (c) Tom Trebisky  11-5-2023
 */

// Summary of how this driver works:
//
//- Enable the USB hardware on STM32
//  - Configure GPIO pins for D+ / D–
//  - Set up PMA (packet memory) and the endpoint buffer table
//
//- Create a circular queue
//  - Stores received data from EP1 OUT
//  - Prevents data loss by allowing deferred processing in the main loop
//
//- Set up `USB_LP_CAN1_RX0_IRQHandler` as the main USB interrupt handler
//  - RESET events: re‑initialise endpoints and set device address to `0`
//  - CTR (Correct Transfer) events:
//    - EP0 traffic to `ctr0()`
//    - Other endpoint traffic to `data_ctr()`
//
//- Endpoint 0 (EP0):  Control endpoint for setup/control calls from the host
//  - Implicit in USB spec (no separate endpoint descriptor)
//  - `ctr0()` handles all EP0 transfers:
//    - SETUP packets: `usb_setup()` to interpret and prepare a response
//    - OUT data stage packets: `usb_control()` to process payload
//  - Used for enumeration, standard USB requests, and CP2102 vendor‑specific
//  commands
//
//- Endpoint 1 (EP1): Bulk data endpoint for device <-> host text transfer
//  - Declared in config descriptor as BULK IN and BULK OUT
//  - OUT: (host -> device):
//    - `data_ctr()` queues incoming data
//    - Later, `endpoint_recv()` (in a loop) pulls data from queue into a
//    processing buffer
//  - IN: (device -> host):
//    - Functions like `usb_terminal_print()` send text back to the host
//
//- Main terminal loop
//  - Polls `cq_count()` to check for queued input or to send asynchronous
//  output
//  - Drains and processes pending data without blocking the main loop

#include "usb.h"
#include "builtins.h"
#include "cqeue.h"
#include "stm32f10x.h"
#include "timer.h"
#include "usb_enum.h"
#include "utils.h"

volatile enum usb_state usb_current_state = BOOT;
enum uart_state uart_current_state = DISABLED;
static void hw_init();
static void reset();
static void set_address(int addr);
static void enum_wait();
static void endpoint_init();
static void endpoint_rem(int ep);
static void endpoint_recv_ready(int ep);
static void endpoint_set_rx_ready(int ep);
static void endpoint_clear_rx(int ep);
static void endpoint_clear_tx(int ep);
static void endpoint_stall(int ep);
static void endpoint_set_tx_valid(int ep);
static void endpoint_set_tx_nak(int ep);
static int endpoint_recv(int ep, char *buf, int limit);
static void endpoint_send(int ep, char *buf, int count);
static void pma_clear();
static void pma_copy_in(uint32_t pma_off, char *buf, int count);
static void pma_copy_out(uint32_t pma_off, char *buf, int count);
static void ctr0();
static void data_ctr(int ep);
static void init_terminal();

#define USB_BASE 0x40005C00
#define USB_RAM 0x40006000

// The addresses are defined in the ld file
struct pma_buf {
  uint32_t buf[32];
};

// 4 * 4 bytes = 16 bytes in ARM addr space.
struct btable_entry {
  uint32_t tx_addr;
  uint32_t tx_count;
  uint32_t rx_addr;
  uint32_t rx_count;
};

extern struct pma_buf PMA_buf[8];
extern struct btable_entry PMA_btable[8];

#define EP_COUNT 8
typedef struct {
  __IO uint32_t EPR[EP_COUNT];
  __IO uint32_t __PADDING[EP_COUNT];
  __IO uint32_t CNTR;
  __IO uint32_t ISTR;
  __IO uint32_t FNR;
  __IO uint32_t DADDR;
  __IO uint32_t BTABLE; // Holds the address of the BTBALE in PMA. It's not the
                        // BTABLE itself.
} USB_TypeDef;
#define USB ((USB_TypeDef *)USB_BASE)

#define EP0R 0
#define EP1R 1

struct endpoint {
  uint8_t flags;
  uint32_t *tx_buf;
  uint32_t *rx_buf;
};

#define F_RX_BUSY 0x01
#define F_TX_BUSY 0x02
#define F_TX_REM 0x80

struct endpoint ep_info[EP_COUNT];

#define IN_BUF_SIZE 256
static struct cqueue in_queue;
static char in_buf[IN_BUF_SIZE];

void usb_init() {
  // enable usb
  RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
  RCC->APB1ENR |= RCC_APB1ENR_USBEN;
  GPIOC->CRH &= ~(GPIO_CRH_CNF11 | GPIO_CRH_MODE11);
  GPIOC->CRH &= ~(GPIO_CRH_CNF12 | GPIO_CRH_MODE12);
  GPIOC->CRH |= (GPIO_CRH_CNF11_0 | GPIO_CRH_CNF11_1);
  GPIOC->CRH |= (GPIO_CRH_MODE11_0 | GPIO_CRH_MODE11_1);
  GPIOC->CRH |= (GPIO_CRH_CNF12_0 | GPIO_CRH_CNF12_1);
  GPIOC->CRH |= (GPIO_CRH_MODE12_0 | GPIO_CRH_MODE12_1);

  cq_init(&in_queue, in_buf, IN_BUF_SIZE);

  hw_init();
  reset();
  usb_current_state = INIT;
  dbg_print("USB: USB initiated\n");

  enum_wait();

  if (usb_current_state != CONFIGURED)
    dbg_print("USB: Enumeration failed (timed out)\n");
  else
    dbg_print("USB: Enumeration succeeded\n");

  init_terminal();
}

#define EP0R_TX_BUF_INDEX 1
#define EP0R_RX_BUF_INDEX 2
#define EP1R_TX_BUF_INDEX 3
#define EP1R_RX_BUF_INDEX 4
#define COUNT_RX_VALUE (0x8000 | 1 << 10)

static int pending_address = 0;
void usb_pend_address(int addr) { pending_address = addr; }

static char *tx_rem;
static int tx_rem_count;
#define ENDPOINT_LIMIT 64
void usb_endpoint_send(int ep, char *buf, int count) {
  struct btable_entry *bte;

  bte = &((struct btable_entry *)USB_RAM)[ep];

  if (count <= ENDPOINT_LIMIT) {
    bte->tx_count = count;
    pma_copy_out(bte->tx_addr, buf, count);
    endpoint_set_tx_valid(ep);
    return;
  }

  tx_rem = &buf[ENDPOINT_LIMIT];
  tx_rem_count = count - ENDPOINT_LIMIT;
  ep_info[ep].flags |= F_TX_REM;

  bte->tx_count = ENDPOINT_LIMIT;
  pma_copy_out(bte->tx_addr, buf, ENDPOINT_LIMIT);
  endpoint_set_tx_valid(ep);
}

// Send a zero length packet
void usb_endpoint_send_zlp(int ep) {
  struct btable_entry *bte;
  bte = &((struct btable_entry *)USB_RAM)[ep];
  bte->tx_count = 0;
  endpoint_set_tx_valid(ep);
}

int usb_endpoint_recv(int ep, char *buf) {
  struct btable_entry *bte;
  int count;

  bte = &((struct btable_entry *)USB_RAM)[ep];

  count = bte->rx_count & 0x3ff;
  pma_copy_in(bte->rx_addr, buf, count);

  return count;
}

static int int_count = 0;
static int int_first = 1;
void USB_LP_CAN1_RX0_IRQHandler() {
  if (int_first && int_count++ > 2000) {
    int_first = 0;
    USB->CNTR = 0;
  }

  if (USB->ISTR & USB_ISTR_RESET) {
    reset();
    USB->ISTR &= ~USB_ISTR_RESET;
  }

  if (USB->ISTR & USB_ISTR_CTR) {
    int ep = USB->ISTR & 0xf;

    if (ep == 0) {
      ctr0();
    } else {
      data_ctr(ep);
    }

    USB->ISTR &= ~USB_ISTR_CTR;
  }
}

static void hw_init() {
  USB->CNTR = 0;
  USB->ISTR = 0;
  USB->DADDR = 0;
  USB->BTABLE = 0;

  NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
  // NVIC_EnableIRQ(USB_HP_CAN1_TX_IRQn);
  // NVIC_EnableIRQ(USBWakeUp_IRQn);

  pma_clear();
  endpoint_init();
  set_address(0);

  // Enable just these two interrupts
  USB->CNTR = USB_ISTR_CTR | USB_ISTR_RESET;
}

static void reset() {
  endpoint_init();
  set_address(0);
}

static void set_address(int addr) { USB->DADDR = USB_DADDR_EF | (addr & 0x7f); }

static void enum_wait() {
  int tmo = 5 * 100;

  while (tmo--) {
    timer_delay_ms(10);
    if (usb_current_state == CONFIGURED)
      break;
  }

  if (usb_current_state != CONFIGURED)
    dbg_print("USB: Enumeration failed (timed out)\n");
}

#define EP_TOGGLE_TX (USB_EP0R_DTOG_RX | USB_EP0R_DTOG_TX | USB_EP0R_STAT_RX)
#define EP_TOGGLE_RX (USB_EP0R_DTOG_RX | USB_EP0R_DTOG_TX | USB_EP0R_STAT_TX)
#define EP_TOGGLE_ALL                                                          \
  (USB_EP0R_DTOG_RX | USB_EP0R_DTOG_TX | USB_EP0R_STAT_TX | USB_EP0R_STAT_RX)
static void endpoint_init() {
  for (int ep = 0; ep < EP_COUNT; ep++)
    USB->EPR[ep] = 0;

  // Endpoint type = CONTROL 0b01 , address = 0
  USB->EPR[EP0R] = USB_EP0R_EP_TYPE_0 | 0;
  // Endpoint type = BULK 0b00 , address = 1
  USB->EPR[EP1R] = 1;

  PMA_btable[EP0R].tx_addr = EP0R_TX_BUF_INDEX * 64;
  PMA_btable[EP0R].tx_count = 0;
  PMA_btable[EP0R].rx_addr = EP0R_RX_BUF_INDEX * 64;
  PMA_btable[EP0R].rx_count = COUNT_RX_VALUE;

  endpoint_set_rx_ready(EP0R);
  endpoint_set_tx_nak(EP0R);

  PMA_btable[EP1R].tx_addr = EP1R_TX_BUF_INDEX * 64;
  PMA_btable[EP1R].tx_count = 0;
  PMA_btable[EP1R].rx_addr = EP1R_RX_BUF_INDEX * 64;
  PMA_btable[EP1R].rx_count = COUNT_RX_VALUE;

  endpoint_set_rx_ready(EP1R);
  endpoint_set_tx_nak(EP1R);

  ep_info[EP0R].tx_buf = PMA_buf[1].buf;
  ep_info[EP0R].rx_buf = PMA_buf[2].buf;
  ep_info[EP0R].flags = 0;

  ep_info[EP1R].tx_buf = PMA_buf[3].buf;
  ep_info[EP1R].rx_buf = PMA_buf[4].buf;
  ep_info[EP1R].flags = 0;
}

static void endpoint_rem(int ep) {
  struct btable_entry *bte;

  bte = &((struct btable_entry *)USB_RAM)[ep];

  bte->tx_count = tx_rem_count;
  pma_copy_out(bte->tx_addr, tx_rem, tx_rem_count);
  endpoint_set_tx_valid(ep);

  ep_info[ep].flags &= ~F_TX_REM;
}

// flag an endpoint ready to receive
static void endpoint_recv_ready(int ep) {
  struct btable_entry *bte;

  bte = &((struct btable_entry *)USB_RAM)[ep];

  bte->rx_count &= ~0x3ff;

  endpoint_set_rx_ready(ep);
}

static void endpoint_set_rx_ready(int ep) {
  uint32_t val;

  val = USB->EPR[ep];

  val &= ~USB_EP0R_CTR_RX;
  val |= USB_EP0R_CTR_TX;
  val &= ~EP_TOGGLE_RX;

  val ^= USB_EP0R_STAT_RX;

  USB->EPR[ep] = val;
}

static void endpoint_clear_rx(int ep) {
  uint32_t val;

  val = USB->EPR[ep];

  val &= ~USB_EP0R_CTR_RX;
  val &= ~EP_TOGGLE_ALL;

  USB->EPR[ep] = val;
}

static void endpoint_clear_tx(int ep) {
  uint32_t val;

  val = USB->EPR[ep];

  val &= ~USB_EP0R_CTR_TX;
  val |= USB_EP0R_CTR_RX;
  val &= ~EP_TOGGLE_ALL;

  USB->EPR[ep] = val;
}

static void endpoint_stall(int ep) {
  uint32_t val;

  val = USB->EPR[ep];

  val &= ~USB_EP0R_CTR_RX | USB_EP0R_CTR_TX;
  val &= ~EP_TOGGLE_RX;

  val ^= USB_EP0R_STAT_TX_0;

  USB->EPR[ep] = val;
}

static void endpoint_set_tx_valid(int ep) {
  uint32_t val;

  val = USB->EPR[ep];

  val &= ~USB_EP0R_CTR_TX;
  val |= USB_EP0R_CTR_RX;
  val &= ~EP_TOGGLE_TX;

  val ^= USB_EP0R_STAT_TX;

  USB->EPR[ep] = val;
}

static void endpoint_set_tx_nak(int ep) {
  uint32_t val;

  val = USB->EPR[ep];

  val &= ~USB_EP0R_CTR_TX;
  val |= USB_EP0R_CTR_RX;
  val &= ~EP_TOGGLE_TX;

  val ^= USB_EP0R_STAT_TX_1;

  USB->EPR[ep] = val;
}

// Handle interrupt driven input
static int endpoint_recv(int ep, char *buf, int limit) {
  int i, n;

  while (cq_count(&in_queue) < 1)
    ;

  n = cq_count(&in_queue);
  if (n > limit)
    n = limit;

  for (i = 0; i < n; i++)
    buf[i] = cq_remove(&in_queue);

  return n;
}

static void endpoint_send(int ep, char *buf, int count) {

  if (usb_current_state != CONFIGURED)
    return;

  if (uart_current_state != ENABLED)
    return;

  dbg_print("USB: Endpoint send initiated ...\n");
  while (ep_info[ep].flags & F_TX_BUSY)
    ;

  dbg_print("USB: Sending ");
  dbg_printi(count);
  dbg_print(" char(s) on endpoint ");
  dbg_printi(ep);
  dbg_print("\n");
  ep_info[ep].flags |= F_TX_BUSY;
  usb_endpoint_send(ep, buf, count);
}

static void pma_clear() {
  uint32_t *p;
  memset((char *)USB_RAM, 0x0, 1024);

  p = (uint32_t *)USB_RAM;
  p[254] = 0xdead;
  p[255] = 0xdeadbeef;
}

static void pma_copy_in(uint32_t pma_off, char *buf, int count) {
  int num = (count + 1) / 2;
  uint16_t *bp = (uint16_t *)buf;
  uint32_t *pp;
  uint32_t addr;

  addr = (uint32_t)USB_RAM;
  addr += 2 * pma_off;
  pp = (uint32_t *)addr;

  for (int i = 0; i < num; i++)
    *bp++ = *pp++;
}

static void pma_copy_out(uint32_t pma_off, char *buf, int count) {
  int num = (count + 1) / 2;
  uint16_t *bp = (uint16_t *)buf;
  uint32_t *pp;
  uint32_t addr;

  addr = (uint32_t)USB_RAM;
  addr += 2 * pma_off;
  pp = (uint32_t *)addr;

  for (int i = 0; i < num; i++)
    *pp++ = *bp++;
}

#define SETUP_BUF 10
static void ctr0() {
  struct btable_entry *bte;
  char buf[SETUP_BUF];
  int count;
  int rv;
  int setup;

  if (USB->EPR[EP0R] & USB_EP0R_CTR_RX) {
    bte = &PMA_btable[EP0R];
    count = bte->rx_count & 0x3ff;

    if (count > SETUP_BUF) {
      dbg_print("USB: Setup too big: ");
      dbg_printi(count);
      dbg_print("\n");
      panic("ctr0 setup count");
    }

    setup = USB->EPR[EP0R] & USB_EP0R_SETUP;

    endpoint_clear_tx(EP0R);
    count = usb_endpoint_recv(EP0R, buf);
    endpoint_recv_ready(EP0R);

    if (count == 1)
      return;

    if (setup) {
      rv = usb_setup(buf, count);
    } else {
      rv = usb_control(buf, count);
    }
  }

  if (USB->EPR[EP0R] & USB_EP0R_CTR_TX) {
    count = PMA_btable[0].tx_count;

    endpoint_clear_tx(EP0R);

    if (pending_address) {
      set_address(pending_address);
      pending_address = 0;
      return;
    }

    if (ep_info[EP0R].flags & F_TX_REM) {
      endpoint_rem(EP0R);
      return;
    }

    ep_info[EP0R].flags &= ~F_TX_BUSY;
  }
}

static void data_ctr(int ep) {
  struct btable_entry *bte;
  char inbuf[64];
  int count;
  int i;

  if (USB->EPR[ep] & USB_EP0R_CTR_TX) {
    dbg_print("USB: Data CTR (Tx) on endpoint ");
    dbg_printi(ep);
    dbg_print(" isr=");
    dbg_printi(USB->ISTR);
    dbg_print(" epr=");
    dbg_printi(USB->EPR[ep]);
    dbg_print("\n");
    endpoint_clear_tx(ep);
    ep_info[ep].flags &= ~F_TX_BUSY;
    return;
  }

  if (USB->EPR[ep] & USB_EP0R_CTR_RX) {
    dbg_print("USB: Data CTR (Rx) on endpoint ");
    dbg_printi(ep);
    dbg_print(" isr=");
    dbg_printi(USB->ISTR);
    dbg_print(" epr=");
    dbg_printi(USB->EPR[ep]);
    dbg_print("\n");
    endpoint_clear_rx(ep);

    count = PMA_btable[ep].rx_count & 0x3ff;
    bte = &((struct btable_entry *)USB_RAM)[ep];
    pma_copy_in(bte->rx_addr, inbuf, count);

    dbg_print("USB: ");
    dbg_printi(count);
    dbg_print(" byte(s) of data received\n");

    for (i = 0; i < count; i++) {
      if (cq_space(&in_queue) > 0)
        cq_add(&in_queue, inbuf[i]);
    }

    endpoint_recv_ready(ep);
  }
}

static void init_terminal() {
  char buf[2];
  int count;

  endpoint_recv_ready(EP1R);
  ep_info[EP1R].flags |= F_RX_BUSY;
  dbg_print("USB: USB terminal initiated\n");

  for (;;) {
    // check if we need to send an initial prompt
    if (usb_inital_prompt_pending && usb_current_state == CONFIGURED &&
        uart_current_state == ENABLED && !(ep_info[EP1R].flags & F_TX_BUSY)) {
      builtins_terminal_init_prompt();
      usb_inital_prompt_pending = 0;
    }

    // Handle any available RX without blocking.
    // The queue gets populated using cq_add under data_ctr.
    // This if block then gets taken and calls endpoint_recv to copy the data
    // into a buffer which is now non-blocking.
    if (cq_count(&in_queue) > 0) {
      int count = endpoint_recv(EP1R, buf, sizeof(buf));
      if (count > 0) {
        if (buf[0] == T_BACKSPACE) {
          builtins_terminal_process_backspace();
        } else if (buf[0] == T_RETURN) {
          builtins_terminal_process_return();
        } else {
          builtins_terminal_add_to_buffer(buf[0]);
          endpoint_send(EP1R, buf, count);
        }
      }
    }

    // small delay to avoid max CPU spin
    timer_delay_ms(1);
  }
}

void usb_terminal_print(char *string) {
  int count = 0;
  char *start = string;
  while (*(start++) != 0) {
    count++;
  }
  endpoint_send(EP1R, string, count);
}
