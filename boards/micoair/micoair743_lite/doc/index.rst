.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: micoair743_lite

Overview
********

The `MicoAir743-Lite`_ is a 30.5 x 30.5 mm drone flight controller from MicoAir Tech., built
around the STMicroelectronics STM32H743VIT6 microcontroller. It combines an ICM-45686 IMU, an
SPA06 barometer, a microSD card slot and an on-board Bluetooth module for telemetry, with 8 UARTs
and 15 PWM outputs for receivers, GNSS, telemetry radios, ESCs and servos.

Hardware
********

- STM32H743VIT6 in LQFP100 package

  - Arm Cortex-M7 core with double-precision FPU, running at 480 MHz
  - 2 MB flash, 1 MB SRAM

- 8 MHz crystal oscillator
- TDK InvenSense ICM-45686 6-axis IMU on SPI3
- Goertek SPA06 barometer on I2C2
- microSD card slot (4-bit SDMMC1)
- Bluetooth module connected to UART8
- 8 UARTs, one of them dedicated to an HD VTX connector (SH1.0 6-pin)
- 15 PWM outputs
- 1 external I2C port
- Battery voltage and current sense inputs
- Red, green and blue status LEDs
- Buzzer output (PD15, active high)
- BOOT button
- USB Type-C connector (USB OTG FS)
- SWD pads
- 2S to 6S (6 V to 27 V) power input, 5 V 2 A and 9 V 2 A BECs
- 36 x 36 x 8 mm, 10 g

Supported Features
==================

.. zephyr:board-supported-hw::

The SPA06 barometer has no Zephyr driver; the I2C2 bus it is connected to is enabled so that
applications can access it directly at address ``0x77``.

The ICM-45686 is not aligned with the board's forward direction: relative to the board frame,
it is rotated 180 degrees in roll and 90 degrees in yaw.

Connections and IOs
===================

Default Zephyr Peripheral Mapping
---------------------------------

.. list-table::
   :header-rows: 1

   * - Peripheral
     - Pins
     - Board function
   * - USART1
     - PA9 (TX), PA10 (RX)
     - TELEM1
   * - USART2
     - PA2 (TX), PA3 (RX)
     - HD VTX connector (DisplayPort)
   * - USART3
     - PD8 (TX), PD9 (RX)
     - GPS
   * - UART4
     - PA0 (TX), PA1 (RX)
     - TELEM2
   * - UART5
     - PB6 (TX), PB5 (RX)
     - General purpose
   * - USART6
     - PC6 (TX), PC7 (RX)
     - RC input
   * - UART7
     - PE8 (TX), PE7 (RX)
     - ESC telemetry
   * - UART8
     - PE1 (TX), PE0 (RX)
     - On-board Bluetooth module
   * - I2C1
     - PB8 (SCL), PB9 (SDA)
     - External I2C port (compass)
   * - I2C2
     - PB10 (SCL), PB11 (SDA)
     - SPA06 barometer, data ready on PD0
   * - SPI3
     - PB3 (SCK), PB4 (MISO), PD6 (MOSI), PD4 (CS)
     - ICM-45686 IMU, interrupt on PD5
   * - SDMMC1
     - PC8-PC11 (D0-D3), PC12 (CK), PD2 (CMD)
     - microSD card
   * - ADC1
     - PC0 (INP10), PC1 (INP11)
     - Battery voltage, battery current
   * - USB OTG FS
     - PA11 (DM), PA12 (DP)
     - USB Type-C connector
   * - GPIO
     - PE3, PE2, PE4
     - Red, green and blue LEDs (active low)

PWM Outputs
-----------

All timers are configured with a 1 MHz counter clock, which covers standard servo and ESC PWM
periods up to 65 ms. Outputs sharing a timer must use the same period.

.. list-table::
   :header-rows: 1

   * - Output
     - Pin
     - Timer channel
     - Devicetree PWM specifier
   * - M1
     - PE14
     - TIM1_CH4
     - ``<&pwm1 4 ...>``
   * - M2
     - PE13
     - TIM1_CH3
     - ``<&pwm1 3 ...>``
   * - M3
     - PE11
     - TIM1_CH2
     - ``<&pwm1 2 ...>``
   * - M4
     - PE9
     - TIM1_CH1
     - ``<&pwm1 1 ...>``
   * - M5
     - PB1
     - TIM3_CH4
     - ``<&pwm3 4 ...>``
   * - M6
     - PB0
     - TIM3_CH3
     - ``<&pwm3 3 ...>``
   * - M7
     - PA7
     - TIM3_CH2
     - ``<&pwm3 2 ...>``
   * - M8
     - PA6
     - TIM3_CH1
     - ``<&pwm3 1 ...>``
   * - M9
     - PE5
     - TIM15_CH1
     - ``<&pwm15 1 ...>``
   * - M10
     - PE6
     - TIM15_CH2
     - ``<&pwm15 2 ...>``
   * - M11
     - PD12
     - TIM4_CH1
     - ``<&pwm4 1 ...>``
   * - M12
     - PD13
     - TIM4_CH2
     - ``<&pwm4 2 ...>``
   * - M13
     - PB14
     - TIM12_CH1
     - ``<&pwm12 1 ...>``
   * - M14
     - PB15
     - TIM12_CH2
     - ``<&pwm12 2 ...>``
   * - M15 (LED pad)
     - PD14
     - TIM4_CH3
     - ``<&pwm4 3 ...>``

System Clock
------------

The system clock is driven by the PLL at 480 MHz, sourced from the 8 MHz external crystal.
The SDMMC kernel clock is 48 MHz, taken from PLL2. USB uses the internal 48 MHz oscillator
(HSI48).

Serial Port
-----------

The board has no debug UART. The Zephyr console and shell are provided over USB CDC ACM on the
USB Type-C connector. All other UARTs are enabled at 115200 baud and are available to
applications.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

The board is flashed over USB using the STM32 system bootloader (USB DFU), which needs no extra
hardware. An external SWD probe can be connected to the SWD pads for flashing and debugging.

.. note::

   Flashing a Zephyr image at the start of flash overwrites the firmware and bootloader that the
   board ships with.

Flashing
========

To enter the STM32 system bootloader, hold the BOOT button while connecting the USB cable. The
board enumerates as a USB DFU device (``0483:df11``) and can be flashed with ``west flash``, which
uses `dfu-util`_ by default.

Here is an example for the :zephyr:code-sample:`blinky` application, which blinks the blue LED:

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: micoair743_lite
   :goals: build flash

The application starts as soon as flashing completes.

To get an interactive shell over USB, flash the :zephyr:code-sample:`shell-module` sample:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/shell/shell_module
   :board: micoair743_lite
   :goals: build flash

Then open the USB CDC ACM serial port, for example ``/dev/ttyACM0`` on Linux:

.. code-block:: console

   $ minicom -D /dev/ttyACM0

The :zephyr:code-sample:`sensor_shell` sample can be used the same way to read the IMU with the
``sensor get icm45686@0`` command.

Debugging
=========

Connect an SWD probe such as an ST-LINK or a SEGGER J-Link to the SWDIO (PA13), SWCLK (PA14)
and GND pads, then use ``west debug``, or ``west flash`` with the matching runner, for example:

.. code-block:: console

   $ west flash --runner jlink

References
**********

- `MicoAir743-Lite`_ product page
- `STM32H743VI on www.st.com`_

.. _MicoAir743-Lite:
   https://micoair.com/flightcontroller_micoair743lite/

.. _dfu-util:
   https://dfu-util.sourceforge.net/

.. _STM32H743VI on www.st.com:
   https://www.st.com/en/microcontrollers-microprocessors/stm32h743vi.html
