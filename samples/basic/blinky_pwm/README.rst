.. zephyr:code-sample:: pwm-blinky
   :name: PWM Blinky
   :relevant-api: pwm_interface

   Blink an LED using the PWM API.

Overview
********

This application blinks an LED using the :ref:`PWM API <pwm_api>`. See
:zephyr:code-sample:`blinky` for a GPIO-based sample.

The LED starts blinking at a 1 Hz frequency. The frequency doubles every 4
seconds until it reaches 128 Hz. The frequency will then be halved every 4
seconds until it returns to 1 Hz, completing a single blinking cycle. This
faster-then-slower blinking cycle then repeats forever.

Some PWM hardware cannot set the PWM period to 1 second to achieve the blinking
frequency of 1 Hz. This sample calibrates itself to what the hardware supports
at startup. The maximum PWM period is decreased appropriately until a value
supported by the hardware is found.

Requirements
************

The board must have an LED connected to a PWM output channel. The PWM
controlling this LED must be configured using the ``pwm_led0`` :ref:`devicetree
<dt-guide>` alias, usually in the :ref:`BOARD.dts file
<devicetree-in-out-files>`.

Wiring
******

No additional wiring is necessary if ``pwm_led0`` refers to hardware that is
already connected to an LED on the board.

In these other cases, however, manual wiring is necessary:

.. list-table::
   :header-rows: 1

   * - Board
     - Wiring
   * - :zephyr:board:`nucleo_f401re`
     - connect PWM2 (PA0) to an LED
   * - :zephyr:board:`nucleo_l476rg`
     - connect PWM2 (PA0) to an LED
   * - :zephyr:board:`stm32f4_disco`
     - connect PWM2 (PA0) to an LED
   * - :zephyr:board:`nucleo_f302r8`
     - connect PWM2 (PA0) to an LED
   * - :zephyr:board:`nucleo_f103rb`
     - connect PWM1 (PA8) to an LED
   * - :zephyr:board:`nucleo_wb55rg`
     - connect PWM1 (PA8) to an LED
   * - :zephyr:board:`esp32_devkitc`
     - connect GPIO2 to an LED
   * - :zephyr:board:`esp32s2_saola`
     - connect GPIO2 to an LED
   * - :zephyr:board:`esp32c3_devkitm`
     - connect GPIO2 to an LED

Building and Running
********************

To build and flash this sample for the :zephyr:board:`nrf52840dk`:

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky_pwm
   :board: nrf52840dk/nrf52840
   :goals: build flash
   :compact:

Change ``nrf52840dk/nrf52840`` appropriately for other supported boards.

After flashing, the sample starts blinking the LED as described above. It also
prints information to the board's console.

Build errors
************

You will see a build error at the source code line defining the ``pwm_led0``
variable if you try to build this sample for an unsupported board.

On GCC-based toolchains, the error looks like this:

.. code-block:: none

   error: '__device_dts_ord_DT_N_ALIAS_pwm_led0_P_pwms_IDX_0_PH_ORD' undeclared here (not in a function)

Adding board support
********************

To add support for your board, create a :ref:`devicetree overlay
<set-devicetree-overlays>` that defines a ``pwm-leds`` node and the
``pwm-led0`` alias. The overlay file should be named
:file:`boards/<your_board>.overlay` in the sample directory, or placed in your
application directory.

A minimal overlay looks like this:

.. code-block:: DTS

   / {
   	aliases {
   		pwm-led0 = &my_pwm_led;
   	};

   	pwmleds {
   		compatible = "pwm-leds";
   		my_pwm_led: my_pwm_led {
   			pwms = <&pwm0 0 PWM_MSEC(20) PWM_POLARITY_NORMAL>;
   		};
   	};
   };

   &pwm0 {
   	status = "okay";
   };

Replace ``&pwm0``, channel ``0``, and ``PWM_POLARITY_NORMAL`` with the values
appropriate for your hardware. The ``pwms`` property uses the format
``<&pwm_controller channel period flags>``.

Tips:

- See :dtcompatible:`pwm-leds` for more information on defining PWM-based LEDs
  in devicetree.

- If you're not sure what to do, check the overlays for boards already
  supported by this sample (in the :zephyr_file:`samples/basic/blinky_pwm/boards`
  directory) that use the same SoC as your target. See
  :ref:`get-devicetree-outputs` for details on inspecting devicetree output.

- See :zephyr_file:`include/zephyr/dt-bindings/pwm/pwm.h` for the flags and
  helper macros (such as ``PWM_MSEC()``) you can use in devicetree.

- If your board uses pin control (``pinctrl``), you may also need to define a
  ``pinctrl`` configuration for the PWM controller. Check your SoC's pin
  control bindings and existing board overlays for reference.

- If the LED is built in to your board hardware, the alias should be defined in
  your :ref:`BOARD.dts file <devicetree-in-out-files>`. Otherwise, define one
  in a :ref:`devicetree overlay <set-devicetree-overlays>`.
