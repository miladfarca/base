/*
 * Forked from https://github.com/trebisky/stm32f103/blob/master/usb/usb_enum.c
 * Changes by: miladfarca, 2025
 *
 * (c) Tom Trebisky  11-22-2023
 */

#include "usb_enum.h"
#include "stm32f10x.h"
#include "usb.h"
#include "utils.h"

extern volatile enum usb_state usb_current_state;
extern enum uart_state uart_current_state;
int usb_inital_prompt_pending = 0;

#define DESC_TYPE_DEVICE 1
#define DESC_TYPE_CONFIG 2
#define DESC_TYPE_STRING 3
#define DESC_TYPE_INTERFACE 4
#define DESC_TYPE_ENDPOINT 5

// Act like we are a CP2102
static uint8_t my_device_desc[] = {
    0x12, // bLength
    DESC_TYPE_DEVICE,
    0x00,
    0x02, // bcdUSB = 2.00
    0x00, // bDeviceClass: 0 (device)
    0x00, // bDeviceSubClass
    0x00, // bDeviceProtocol
    0x40, // bMaxPacketSize0

    0xc4, // idVendor = 0x10c4 (silicon labs)
    0x10,
    0x60, // idProduct = 0xEA60 (CP210x uart bridge)
    0xea, //

    0x00, // bcdDevice = 1.00
    0x01, //

    1, // Index of string descriptor describing manufacturer
    2, // Index of string descriptor describing product
    3, // Index of string descriptor describing device serial number
    1  // bNumConfigurations
};

static uint8_t my_config_desc[] = {
    // Configuration Descriptor
    0x09, // bLength: Configuration Descriptor size
    DESC_TYPE_CONFIG,
    0x20, // wTotalLength: including sub-descriptors
    0x00, //      "      : MSB of uint16_t

    0x01, // bNumInterfaces: 1
    0x01, // bConfigurationValue: 1
    0x00, // iConfiguration: Index of string descriptor for configuration
    0xC0, // bmAttributes: self powered (CP2102 would use 0x80)
    0x32, // MaxPower 0 mA

    // Interface Descriptor
    0x09, // bLength: Interface Descriptor size
    // static_cast<uint8_t>(UsbDev::DescriptorType::INTERFACE),
    DESC_TYPE_INTERFACE, // Interface descriptor type
    0x00,                // bInterfaceNumber: Number of Interface
    0x00,                // bAlternateSetting: Alternate setting
    2,                   // bNumEndpoints: 2
    0xff,                // bInterfaceClass: Vendor specific
    0x00,                // bInterfaceSubClass:
    0x00,                // bInterfaceProtocol:
    0x02,                // iInterface: (weird)

#define DATA_ENDPOINT_OUT 1
#define DATA_ENDPOINT_IN 1

#define ACM_DATA_SIZE 8
#define CDC_OUT_DATA_SIZE 64
#define CDC_IN_DATA_SIZE 64

#define ENDPOINT_DIR_IN 0x80
#define ENDPOINT_TYPE_BULK 2
#define ENDPOINT_TYPE_INTERRUPT 3

    // Endpoint 1 Descriptor
    0x07, // bLength: Endpoint Descriptor size
    DESC_TYPE_ENDPOINT,
    DATA_ENDPOINT_IN | ENDPOINT_DIR_IN, // bEndpointAddress
    ENDPOINT_TYPE_BULK,                 // bmAttributes: Bulk
    64,                                 // wMaxPacketSize:
    0x00,                               // ^ MSB
    0x00,                               // bInterval

    // Endpoint 1 Descriptor
    0x07, // bLength: Endpoint Descriptor size
    DESC_TYPE_ENDPOINT,
    DATA_ENDPOINT_OUT,  // bEndpointAddress: (OUT3)
    ENDPOINT_TYPE_BULK, // bmAttributes: Bulk
    64,                 // wMaxPacketSize: 64
    0x00,               // ^ MSB
    0x00                // bInterval: ignore for Bulk transfer
};

// There is a 16 bit language id we need to send.
// Wireshark recognizes0x0409 as " English (United States)"
static uint8_t my_language_string_desc[] = {4, DESC_TYPE_STRING, 0x09, 0x04};

struct setup {
  uint8_t rtype;
  uint8_t request;
  uint16_t value;
  uint16_t index;
  uint16_t length;
};

static int device_request(struct setup *);
static int descriptor_request(struct setup *);
static int interface_request(struct setup *);
static void usb_class(struct setup *);

static void would_send(char *, char *, int);

#define RT_RECIPIENT 0x1f
#define RT_TYPE (0x3 << 5)
#define RT_DIR 0x80

static int get_descriptor(struct setup *);
static int set_address(struct setup *);
static int set_configuration(struct setup *);
static int string_send(int);

static int cp21_vendor(struct setup *);
static int cp21_enable(struct setup *);
static int cp21_set_baud(struct setup *);
static int cp21_set_line(struct setup *);
static int cp21_set_chars(struct setup *);
static int cp21_set_modem(struct setup *sp);
static int cp21_get_flow(struct setup *);
static int cp21_get_modem(struct setup *);
static int cp21_get_status(struct setup *sp);

enum cp21_control { NONE, BAUD, CHARS };

enum cp21_control cp21_control = NONE;

static char cp21_baud[4];
static char cp21_chars[6];

int usb_setup(char *buf, int count) {
  struct setup *sp;
  int tag;
  int rv = 0;

  if (usb_current_state == CONFIGURED) {
    dbg_print("USB: Setup packet: ");
    dbg_printi(count);
    dbg_print(" bytes");
    dbg_print("\n");
  }

  // Just ignore ZLP (zero length packets)
  if (count == 0)
    return 0;

  sp = (struct setup *)buf;

  if (sp->rtype == 0x21) {
    usb_class(sp);
    return 1;
  }

  tag = sp->rtype << 8 | sp->request;

  // reset this.
  cp21_control = NONE;

  switch (tag) {
  case 0x8006:
    rv = get_descriptor(sp);
    break;
  case 0x0005:
    rv = set_address(sp);
    break;
  case 0x0009:
    rv = set_configuration(sp);
    break;
  case 0xc0ff:
    rv = cp21_vendor(sp);
    break;
  case 0x4100:
    rv = cp21_enable(sp);
    break;
  case 0x411e:
    rv = cp21_set_baud(sp);
    break;
  case 0x4103:
    rv = cp21_set_line(sp);
    break;
  case 0x4119:
    rv = cp21_set_chars(sp);
    break;
  case 0xc114:
    rv = cp21_get_flow(sp);
    break;
  case 0xc108:
    rv = cp21_get_modem(sp);
    break;
  case 0x4107:
    rv = cp21_set_modem(sp);
    break;
  case 0xc110:
    rv = cp21_get_status(sp);
    break;
  default:
    break;
  }

  return rv;
}

#define D_DESC 1
#define D_CONFIG 2
#define D_STRING 3
#define D_QUAL 6

static uint8_t part_number[] = {2, 0};

static int cp21_vendor(struct setup *sp) {
  usb_endpoint_send(0, part_number, 1);
}

static int cp21_enable(struct setup *sp) {
  if (sp->value == 1) {
    uart_current_state = ENABLED;
    dbg_print("USB: Uart enabled\n");
  } else {
    uart_current_state = DISABLED;
    dbg_print("USB: Uart disabled\n");
  }
  usb_endpoint_send_zlp(0);
}

static int cp21_set_baud(struct setup *sp) {
  cp21_control = BAUD;
  usb_endpoint_send_zlp(0);
}

static int cp21_set_chars(struct setup *sp) {
  cp21_control = CHARS;
  usb_endpoint_send_zlp(0);
}

static int cp21_set_modem(struct setup *sp) {
  if (sp->value & 0x01) {
    dbg_print("USB: Terminal connected (DTR set)\n");
    usb_inital_prompt_pending = 1;
  }

  usb_endpoint_send_zlp(0);
  return 1;
}

static int cp21_set_line(struct setup *sp) { usb_endpoint_send_zlp(0); }

static int cp21_get_flow(struct setup *sp) {
  // Just acknowledge, no data
  usb_endpoint_send_zlp(0);
  return 1;
}

static uint8_t modem_status[] = {0, 0};

static int cp21_get_modem(struct setup *sp) {
  usb_endpoint_send(0, modem_status, 1);
}

static uint8_t cp21_status_response[] = {0x00, 0x00}; // Dummy status

static int cp21_get_status(struct setup *sp) {
  usb_endpoint_send(0, cp21_status_response, sizeof(cp21_status_response));
  return 1;
}

static int get_descriptor(struct setup *sp) {
  int len;
  int type;
  int index;

  // Thanks to the idiot USB business of using the 2 byte
  // value field to hold a 1 byte value and a 1 byte index
  // when we are dealing with a "get descriptor"
  type = sp->value >> 8;
  index = sp->value & 0xff;

  switch (type) {

  case D_DESC:
    usb_endpoint_send(0, my_device_desc, sizeof(my_device_desc));
    return 1;

  case D_QUAL:
    usb_endpoint_send_zlp(0);
    return 1;

  case D_CONFIG:
    len = sizeof(my_config_desc);
    if (len > sp->length)
      len = sp->length;

    if (len < 64) {
      usb_endpoint_send(0, my_config_desc, len);
      return 1;
    }

    usb_endpoint_send(0, my_config_desc, len);
    return 1;

  case D_STRING:
    len = string_send(index);
    return len;

  default:
    break;
  }

  return 0;
}

struct string_xx {
  uint8_t length;
  uint8_t type;
  uint16_t buf[31];
};

static uint8_t *my_strings[] = {"---", "ACME computers", "Basic console port",
                                "1234"};

static int string_send(int index) {
  struct string_xx xx;
  uint8_t *str;
  int n;
  int i;
  int len;

  if (index == 0) {
    len = sizeof(my_language_string_desc);
    usb_endpoint_send(0, my_language_string_desc, len);
    return 1;
  }

  if (index < 1 || index > 3)
    panic("No such string");

  str = my_strings[index];
  n = strlen(str);

  if (n > 31)
    panic("String too big");

  xx.length = 2 + 2 * n;
  xx.type = D_STRING;

  /* 8 bit ascii to 16 bit unicode */
  for (i = 0; i < n; i++)
    xx.buf[i] = str[i];

  len = 2 + 2 * n;
  usb_endpoint_send(0, (char *)&xx, len);

  return 3;
}

static void usb_class(struct setup *sp) { usb_endpoint_send_zlp(0); }

static int set_address(struct setup *sp) {
  // We need to defer this.
  usb_pend_address(sp->value);
  usb_endpoint_send_zlp(0);
  return 1;
}

static int set_configuration(struct setup *sp) {
  usb_endpoint_send_zlp(0);
  usb_current_state = CONFIGURED;
  return 1;
}

int usb_control(char *buf, int count) {
  if (cp21_control == BAUD)
    memcpy(cp21_baud, buf, count);
  else if (cp21_control == CHARS)
    memcpy(cp21_chars, buf, count);
  else {
    dbg_print("USB: Control packet: ");
    dbg_printi(count);
    dbg_print(" bytes");
    dbg_print("\n");
    usb_endpoint_send_zlp(0);
  }

  return 1;
}
