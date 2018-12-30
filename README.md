ws2812-explorer - USB firmware to bootstrap WS2812 projects
===========================================================

ws2812-explorer is a simple firmware for STM32F1 series boards
to manage individually addressable LEDs. Is uses the SPI peripheral
to draw the waveform needed to communicate with the LEDs.

It should work with both branded strips from hobby suppliers and
unbranded WS2812 strips from eBay suppliers.

Quickstart for STM32F103 boards
-------------------------------

- Install the [Arm Embedded
  Toolchain](https://developer.arm.com/open-source/gnu-toolchain/gnu-rm)
  (or similar arm-none-eabi- compiler), openocd and dfu-util.
- Clone this repo:
  `git clone https://github.com/daniel-thompson/ws2812-explorer.git`
- Fetch the libraries:
  `cd ws2812-explorer; git submodule update --init --recursive`
- Build the firmware:
  `make`
- Use JTAG/SWD programmer to install the booloader. Either
  `make -C src/bootloader flash` for STLink v2 or
  `make -C src/bootloader BMP_PORT=/dev/ttyACM0 flash` for a Black Magic Probe.
- Use the bootloader to install the main application using the microcontroller
  board's own USB socket:
  `make -C src/ws2812-explorer flash`

Hardware setup
--------------

The firmware can be easily ported but has been most heavily tested on
STM32F103C8T6 based breakout boards. Connections required are:

- USB to host computer
- 5v and GND to WS2812 chain
- GPIOA7 -> 3.3/5v level shifter -> WS2812 chain

All the GPIO connections are on one side of the controller board (and the
first seven are sequentially allocated).

Usage
-----

The firmware registers itself as a CDC-ACM device and, under Linux, will
be presented as ttyACMx where x is typically 0 on "simple" machines
although will be larger if there is any USB communication or modem device
installed (for example an unused 3G modem).

Type `help` to get a list of available commands. On STM32F1 platforms
the commands available are:

- `echo` - test command that echos back its input
- `id` - reports a unique device serial number (the same number is also
  reported as the USB serial number)
- `help` - shows a list of commands
- `led` - turn the on-board LED on/off
- `reboot` - reboot into DFU mode to allows a firmware upgrade (firmware
  upgrade mode can also be triggered by resetting the board whilst leaving
  it plugged into the host)
- `set` - push out values to the LED chain
- `repeat` - push out and repeat values to the LED chain
- `version` - reports firmware version and build date
- `uptime` - show time since power on

udev rules
----------

The firmware uses the STM32 unique device ID to provide every physical
instance with a unique serial number. This allows udev rules to be
introduced to ensure stable device enumeration regardless of any changes
to the USB topology.

    SUBSYSTEM=="tty", ATTRS{manufacturer}=="redfelineninja.org.uk", ATTRS{serial}=="045101780587252555FFC660", SYMLINK+="ttyLED0"
    SUBSYSTEM=="tty", ATTRS{manufacturer}=="redfelineninja.org.uk", ATTRS{serial}=="7301C2152B72E52DE2744F3B", SYMLINK+="ttyLED1"

The serial number can picked up from the kernel log (`dmesg`) or using udevadm:
`udevadm info --attribute-walk /dev/ttyACM0 | grep serial` .

udevadm can also be used to re-apply the rule if any changes are made to the symlink: `sudo udevadm trigger /dev/ttyACM0`
