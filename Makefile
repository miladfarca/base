# Copyright (c) 2025 miladfarca
#
# Docker build env
#
# apt install gcc-arm-none-eabi binutils-arm-none-eabi stlink-tools clang-format
#
TOOLS = arm-none-eabi
AS = $(TOOLS)-as
LD = $(TOOLS)-ld.bfd
OBJCOPY = $(TOOLS)-objcopy
CC = $(TOOLS)-gcc -mcpu=cortex-m3 -mthumb -fno-builtin-printf -fno-builtin -I$(CURDIR)/src/include -DSTM32F10X_MD -O2

OBJS = src/CMSIS/startup_stm32f10x_md.o \
       src/CMSIS/system_stm32f10x.o \
       src/main.o \
       src/driver/usb.o \
       src/driver/usb_enum.o \
       src/driver/usart.o \
       src/driver/timer.o \
       src/utils/utils.o \
       src/utils/cqeue.o \
       src/builtins/terminal.o \
       src/builtins/help.o \
       src/builtins/echo.o

all: base.bin base.elf

dbg: CC += -DDEBUG
dbg: base.bin base.elf

base.bin:	base.elf
	$(OBJCOPY) base.elf base.bin -O binary
	st-flash write base.bin 0x8000000

base.elf: 	$(OBJS)
	$(LD) -T src/linker.ld -o base.elf $(OBJS)

src/CMSIS/startup_stm32f10x_md.o: src/CMSIS/startup_stm32f10x_md.s
	$(AS) src/CMSIS/startup_stm32f10x_md.s -o src/CMSIS/startup_stm32f10x_md.o

format:
	find src/ \( -iname '*.h' -o -iname '*.c' \) -print0 \
  	| grep -vz -e 'src/CMSIS/system_stm32f10x.c' \
             -e 'src/include/core_cm3.h' \
             -e 'src/include/stm32f10x.h' \
             -e 'src/include/system_stm32f10x.h' \
  	| xargs -0 clang-format -i

clean:
	find . -name '*.o' -type f -delete
	rm -f *.elf *.bin
