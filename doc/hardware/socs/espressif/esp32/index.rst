.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. zephyr:soc-family:: espressif_esp32

Espressif ESP32 SoC Family
###########################

This page provides generic documentation for the :zephyr:soc-family:`espressif_esp32` family.

.. toctree::
   :maxdepth: 1

   esp32
   esp32s2
   esp32s3
   esp32c2
   esp32c3
   esp32c5
   esp32c6
   esp32h2

.. zephyr:soc-fragment:: system-requirements

   Binary Blobs
   ============

   Espressif HAL requires RF binary blobs in order work. Run the command
   below to retrieve those files.

   .. code-block:: console

      west blobs fetch hal_espressif

   .. note::

      It is recommended running the command above after :file:`west update`.

.. zephyr:soc-fragment:: building-flashing

   Simple Boot
   ===========

   The board could be loaded using the single binary image, without 2nd stage bootloader.
   It is the default option when building the application without additional configuration.

   .. note::

      Simple boot does not provide any security features nor OTA updates.

   MCUboot Bootloader
   ==================

   User may choose to use MCUboot bootloader instead. In that case the bootloader
   must be built (and flashed) at least once.

   There are two options to be used when building an application:

   1. Sysbuild
   2. Manual build

   .. note::

      User can select the MCUboot bootloader by adding the following line
      to the board default configuration file.

      .. code:: cfg

         CONFIG_BOOTLOADER_MCUBOOT=y

   Sysbuild
   ========

   The sysbuild makes possible to build and flash all necessary images needed to
   bootstrap the board with the ESP32 SoC.

   To build the sample application using sysbuild use the command:

   .. zephyr-app-commands::
      :tool: west
      :zephyr-app: samples/hello_world
      :board: <board>
      :goals: build
      :west-args: --sysbuild
      :compact:

   By default, the ESP32 sysbuild creates bootloader (MCUboot) and application
   images. But it can be configured to create other kind of images.

   Build directory structure created by sysbuild is different from traditional
   Zephyr build. Output is structured by the domain subdirectories:

   .. code-block::

     build/
     ├── hello_world
     │   └── zephyr
     │       ├── zephyr.elf
     │       └── zephyr.bin
     ├── mcuboot
     │    └── zephyr
     │       ├── zephyr.elf
     │       └── zephyr.bin
     └── domains.yaml

   .. note::

      With ``--sysbuild`` option the bootloader will be re-build and re-flash
      every time the pristine build is used.

   For more information about the system build please read the :ref:`sysbuild` documentation.

   Manual Build
   ============

   During the development cycle, it is intended to build & flash as quickly possible.
   For that reason, images can be built one at a time using traditional build.

   The instructions following are relevant for both manual build and sysbuild.
   The only difference is the structure of the build directory.

   .. note::

      Remember that bootloader (MCUboot) needs to be flash at least once.

   Build and flash applications as usual (see :ref:`build_an_application` and
   :ref:`application_run` for more details).

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: <board>
      :goals: build

   The usual ``flash`` target will work with the board configuration.
   Here is an example for the :zephyr:code-sample:`hello_world`
   application.

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: <board>
      :goals: flash

   Open the serial monitor using the following command:

   .. code-block:: shell

      west espressif monitor

   After the board has automatically reset and booted, you should see the following
   message in the monitor:

   .. code-block:: console

      ***** Booting Zephyr OS vx.x.x-xxx-gxxxxxxxxxxxx *****
      Hello World! <board>

   .. _`Zephyr Support Status`: https://developer.espressif.com/software/zephyr-support-status/

.. zephyr:soc-fragment:: board-variants

   Board variants using Snippets
   =============================

   ESP32 boards can be assembled with different modules using multiple combinations of SPI flash sizes, PSRAM sizes and PSRAM modes.
   The snippets under ``snippets/espressif`` provide a modular way to apply these variations at build time without duplicating board definitions.

   The following snippet-based variants are supported:

   ==========================  =========================
   Snippet name                Description
   ==========================  =========================
   *Flash memory size*
   -----------------------------------------------------
   ``espressif-flash-4M``      Board with 4MB of flash
   ``espressif-flash-8M``      Board with 8MB of flash
   ``espressif-flash-16M``     Board with 16MB of flash
   ``espressif-flash-32M``     Board with 32MB of flash
   ``espressif-flash-64M``     Board with 64MB of flash
   ``espressif-flash-128M``    Board with 128MB of flash

   *PSRAM memory size*
   -----------------------------------------------------
   ``espressif-psram-2M``      Board with 2MB of PSRAM
   ``espressif-psram-4M``      Board with 4MB of PSRAM
   ``espressif-psram-8M``      Board with 8MB of PSRAM

   *PSRAM utilization*
   -----------------------------------------------------
   ``espressif-psram-reloc``   Relocate flash to PSRAM
   ``espressif-psram-wifi``    Wi-Fi buffers in PSRAM
   ==========================  =========================

   To apply a board variant, use the ``-S`` flag with west build:

   .. zephyr-app-commands::
      :tool: west
      :zephyr-app: samples/hello_world
      :board: <board>
      :goals: build
      :snippets: espressif-flash-32M,espressif-psram-4M
      :compact:

   .. note::

      These snippets are only applicable to boards with compatible hardware support for the selected flash/PSRAM configuration.

      - If no FLASH snippet is used, the board default flash size will be used.
      - If no PSRAM snippet is used, the board default psram size will be used.

.. zephyr:soc-fragment:: openocd-debugging

   OpenOCD Debugging
   =================

   Espressif chips require a custom OpenOCD build with ESP32-specific patches.
   Download the latest release from `OpenOCD for ESP32`_.

   For detailed JTAG setup instructions, see `JTAG debugging for ESP32`_.

   Zephyr Thread Awareness
   -----------------------

   OpenOCD supports Zephyr RTOS thread awareness, allowing GDB to:

   - List all threads with ``info threads``
   - Display thread names, priorities, and states
   - Switch between thread contexts
   - Show backtraces for any thread

   **Requirements:**

   - `OpenOCD ESP32 v0.12.0-esp32-20251215`_ or later
   - Build with ``CONFIG_DEBUG_THREAD_INFO=y``

   **Example:**

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: <board>
      :goals: debug
      :gen-args: -DCONFIG_DEBUG_THREAD_INFO=y -DOPENOCD=<path/to/bin/openocd> -DOPENOCD_DEFAULT_PATH=<path/to/openocd/share/openocd/scripts>

   Using a Custom OpenOCD
   ----------------------

   The Zephyr SDK includes a bundled OpenOCD, but it may not have ESP32 support.
   To use the Espressif OpenOCD, specify the path when building:

   .. zephyr-app-commands::
      :zephyr-app: samples/hello_world
      :board: <board>
      :goals: debug
      :gen-args: -DOPENOCD=/path/to/openocd -DOPENOCD_DEFAULT_PATH=/path/to/openocd/scripts


   .. _`OpenOCD for ESP32`: https://github.com/espressif/openocd-esp32/releases
   .. _`OpenOCD ESP32 v0.12.0-esp32-20251215`: https://github.com/espressif/openocd-esp32/releases/tag/v0.12.0-esp32-20251215
   .. _`JTAG debugging for ESP32`: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/jtag-debugging/index.html
