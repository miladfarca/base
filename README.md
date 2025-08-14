## Base
### Overview
Base is a firmware for the STM32 "Blue Pill" (STM32F103C8T6) development board, built using the Arm CMSIS library. It is designed to let you control and program the board entirely through a connected terminal, in the style of a Unix shell. Right now, Base provides a responsive USB‑serial command line for interacting with the board's hardware in real time. In the long term, the goal is to evolve Base into a self‑contained ARM‑based computing environment, a miniature workstation you can connect to, develop on, and run programs directly within, without relying on an external PC for compiling or execution. This will make the Blue Pill function not just as a device you control, but as a complete, always‑ready computer in its own right.

### Build and Flash
Run `make` to compile the firmware and automatically flash base.bin to your Blue Pill using `st-flash`.

### Connect and Use
1. Plug the Blue Pill board into your PC via USB
2. Open a serial terminal such as `picocom /dev/ttyUSB0`
3. Once connected type `help` to see the list of available commands

### Debug Mode
Run `make dbg` to build with debug output enabled.
Connect a USB UART adapter to pin A9 (TX) at `9600` baud rate to see the output.

### Notes
The firmware currently emulates a CP2102 USB serial device for driver free operation on most operating systems.
It is intended for development and educational purposes.

### License
See the LICENSE file in the project root for terms.
