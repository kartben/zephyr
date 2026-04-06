.. zephyr:code-sample:: wuc
   :name: Wake-up Controller (WUC)
   :relevant-api: wuc_interface

   Enable wakeup sources and cycle through low-power sleep/wake states.

Overview
********

This sample demonstrates the :ref:`Wake-up Controller (WUC) API <wuc_api>`.
It enables one or more wakeup sources declared in the board's
``/zephyr,user`` devicetree node, puts the system to sleep, and logs which
sources triggered the wakeup.

The WUC API abstracts hardware wakeup controllers (e.g. the NXP LLWU) so
that application code does not need to know the underlying peripheral
details.  A wakeup source is any hardware signal — a GPIO button, a
timer, a UART activity line — that can bring the SoC out of a low-power
state.

Requirements
************

A board with a WUC driver and at least one wakeup source declared in its
``/zephyr,user`` devicetree node:

.. code-block:: devicetree

   / {
       zephyr_user: zephyr,user {
           wakeup-ctrls = <&wuc0 0>;
       };
   };

See the :dtcompatible:`nxp,llwu` binding for the cell encoding used by
the NXP LLWU driver.

Building and Running
********************

The sample requires a board overlay that populates
``/zephyr,user { wakeup-ctrls = … ; }``.  Create a file
``boards/<your_board>.overlay`` in the sample directory and add the
appropriate Devicetree fragment for your board.

Build and flash the sample:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/wuc
   :board: <your_board>
   :goals: build flash
   :compact:

Sample Output
*************

After flashing, open a serial terminal.  Press the button or trigger the
wakeup pin to observe the sleep/wake cycle:

.. code-block:: console

   [00:00:00.000,000] <inf> wuc_sample: WUC sample started (1 wakeup source(s))
   [00:00:00.000,000] <inf> wuc_sample: Enabled 1 wakeup source(s)
   [00:00:00.000,000] <inf> wuc_sample: Cycle 1: entering low-power state
   [00:00:03.512,000] <inf> wuc_sample: Cycle 1: woke up
   [00:00:03.512,000] <inf> wuc_sample:   Wakeup source 0 triggered
   [00:00:03.512,000] <inf> wuc_sample: Cycle 1: re-enabling wakeup sources
   [00:00:03.512,000] <inf> wuc_sample: Enabled 1 wakeup source(s)
   [00:00:03.512,000] <inf> wuc_sample: Cycle 2: entering low-power state

If no external event arrives, the system wakes from the 5-second
:c:func:`k_sleep` timeout and continues to the next cycle.

Configuration Options
*********************

:kconfig:option:`CONFIG_WUC`
   Enable the WUC driver subsystem.

:kconfig:option:`CONFIG_PM`
   Enable the Zephyr power management subsystem so that :c:func:`k_sleep`
   can enter hardware low-power states instead of spinning.
