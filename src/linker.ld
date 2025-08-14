/* Highest address of the user mode stack */
/* At reset the vector table's first entry (address 0x0) is the stack pointer value, will get loaded to sp register:
   https://developer.arm.com/documentation/ddi0403/d/System-Level-Architecture/System-Level-Programmers--Model/ARMv7-M-exception-model/The-vector-table */
_estack = 0x20004FFF;    /* end of RAM */

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 64K
  RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 20K
}

PERIPH_BASE = 0x40000000;
APB1PERIPH = PERIPH_BASE;
USB    = APB1PERIPH + 0x5C00;
USB_PMA = APB1PERIPH + 0x6000;

PMA_buf = USB_PMA;
PMA_btable = USB_PMA;

SECTIONS
{
  /* Order of sections matter, this will be placed at beginning of FLASH address defined above:
     https://users.informatik.haw-hamburg.de/~krabat/FH-Labor/gnupro/5_GNUPro_Utilities/c_Using_LD/ldLinker_scripts.html (look under order of sections).
     Note beginning of flash is alias to address 0x0 (depending on BOOT pins), look under `Memory mapping` in the datasheet. */
  .isr_vector : {
      KEEP(*(.isr_vector))
  } > FLASH

  .text : {
      *(.text)
      *(.rodata)
  } > FLASH

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  .data : {
  /* create a global symbol at data start and end */
  _sdata = .;
      *(.data)
  _edata = .;
  } > RAM AT> FLASH

  .bss : {
  /* define a global symbol at bss start and end */
  _sbss = .;
      *(.bss)
      *(COMMON)
  _ebss = .;
  } > RAM
}
