.. zephyr:code-sample:: rpi-logic-analyzer
   :name: PIO logic analyzer
   :relevant-api: pio_rpi_pico_interface

   Sample GPIO pins with a PIO state machine and DMA.

Overview
********

This sample turns a Raspberry Pi Pico or Pico 2 into a small logic analyzer.
A PIO state machine samples a contiguous run of pins and DMA copies each
sample into SRAM. The CPU arms the transfer and prints the buffer after the
state machine stops, so the sample rate does not depend on the scheduler.

Free-run capture executes one ``IN PINS, 32`` instruction per sample. The
sample rate is the PIO clock: ``clk_sys`` divided by the PIO clock divider.
Edge capture loops two instructions per sample (the pin read and the trigger
test), so its rate is half of that. On the stock clock that is:

.. list-table::
   :header-rows: 1

   * - Board
     - ``clk_sys``
     - Free-run
     - Edge trigger
   * - Raspberry Pi Pico (RP2040)
     - 125 MHz
     - 125 Msps
     - 62.5 Msps
   * - Raspberry Pi Pico 2 (RP2350)
     - 150 MHz
     - 150 Msps
     - 75 Msps

Raising ``clk_sys`` raises both rates by the same factor. Optional overlays
in this directory set 200 MHz (200 Msps free-run, 100 Msps edge). That is an
overclock. It is not the default, and 400 MHz is not configured.

The capture buffer holds 8192 samples. Channels are GPIO2 through GPIO22.
GPIO22 is also the trigger pin and is bit 20 of each sample word. Bit 0 is
GPIO2. GPIO0 and GPIO1 stay on the console UART. Pins are pulled down and
are 3.3 V only.

Edge mode keeps a ring of the most recent samples until GPIO22 matches the
requested level, then records the requested number of post-trigger samples.
``la dump`` prints the buffer in chronological order.

This is not the `gusmanb/logicanalyzer <https://github.com/gusmanb/logicanalyzer>`_
firmware. That project is GPL-3.0-only, so its sources are not imported
here. The Windows viewer and its binary protocol are unchanged and will not
talk to this sample. The capture method is the same one that project uses:
PIO reads the pins, DMA fills SRAM, and the rate is the PIO clock divided
by the number of instructions in the loop.

Requirements
************

One of:

* Raspberry Pi Pico (``rpi_pico``)
* Raspberry Pi Pico 2 (``rpi_pico2/rp2350a/m33_0`` or
  ``rpi_pico2/rp2350a/hazard3_0``)

Wire signals to GPIO2-GPIO22. The console is UART0 on GPIO0 (TX) and GPIO1
(RX) at 115200 8N1. RP2350 GPIO erratum E9 can latch an input that is driven
high while the pad pull-down is enabled. This sample does not work around
that erratum.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/raspberrypi/logic_analyzer
   :board: rpi_pico
   :goals: build flash
   :compact:

The Pico 2 image is the same application:

.. zephyr-app-commands::
   :zephyr-app: samples/boards/raspberrypi/logic_analyzer
   :board: rpi_pico2/rp2350a/m33_0
   :goals: build flash
   :compact:

To overclock an RP2040 board to 200 MHz, pass the matching overlay. The
flash clock moves with ``clk_sys``; if the board fails to boot, drop the
overlay.

.. code-block:: console

   west build -b rpi_pico -p always samples/boards/raspberrypi/logic_analyzer -- \
     -DDTC_OVERLAY_FILE=sysclk_200mhz_rp2040.overlay

Use ``sysclk_200mhz_rp2350.overlay`` for a Pico 2. The shell starts after a
64-sample boot capture:

.. code-block:: console

   la> la info
   la> la free 10000000 256
   la> la edge 5000000 128 1
   la> la dump 32

``la free`` records a fixed window. ``la edge`` waits up to 30 seconds for
GPIO22 to be the requested level (``1`` high, ``0`` low) and then keeps
``post`` samples after that point. Rates that are not an integer division of
the PIO clock are rounded to the nearest divider step, and the capture
report prints the rate that was actually used.

Sample Output
*************

.. code-block:: console

   logic analyzer ready
   sysclk 125000000 Hz
   free-run max 125000000 sps, 1 cycle/sample
   edge max 62500000 sps, 2 cycles/sample
   channels GPIO2-GPIO22, trigger GPIO22 is bit 20
   buffer 8192 samples
   captured 64 samples at 1000000 Hz (requested 1000000 Hz)
   clkdiv 125 + 0/256, 1 PIO cycles/sample
   GPIO2 edges 0
   sample 0 0x00000000
   boot capture complete
   la> la free 10000000 4
   captured 4 samples at 10000000 Hz (requested 10000000 Hz)
   clkdiv 12 + 128/256, 1 PIO cycles/sample
   GPIO2 edges 1
   sample 0 0x00000000
   sample 1 0x00000001
