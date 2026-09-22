.. zephyr:code-sample:: watchdog
   :name: Watchdog
   :relevant-api: watchdog_interface

   Use the watchdog driver API to reset the board when it gets stuck in an infinite loop.

Overview
********

This sample demonstrates how to use the watchdog driver API.

A typical use case for a watchdog is that the board is restarted in case some piece of code
is kept in an infinite loop.

Requirements
************

The board must provide a ``watchdog0`` devicetree alias pointing to an enabled watchdog node.
Boards that need extra devicetree or Kconfig settings for this sample provide them in the
sample's :file:`boards` directory.

The generic build variant supports the following boards, listed in its
``platform_allow`` entry in :file:`tests.yaml`:
``adp_xc7k/ae350``, ``adp_xc7k/ae350/clic``, ``bl54l15_dvk/nrf54l10/cpuapp``,
``bl54l15_dvk/nrf54l15/cpuapp``, ``bl54l15_dvk/nrf54l15/cpuflpr``,
``bl54l15_dvk/nrf54l15/cpuflpr/xip``, ``cc1352r1_launchxl``, ``cc26x2r1_launchxl``, ``ch32v003evt``,
``ch32v006evt``, ``esp32_devkitc/esp32/procpu``, ``esp32s2_saola``, ``intel_adl_crb``,
``intel_adl_rvp``, ``intel_btl_s_crb``, ``intel_ehl_crb``, ``intel_rpl_p_crb``, ``intel_rpl_s_crb``,
``mps2/an385``, ``nrf5340dk/nrf5340/cpuapp``, ``nrf5340dk/nrf5340/cpunet``,
``nrf54h20dk/nrf54h20/cpuapp``, ``nrf54l15dk/nrf54l05/cpuapp``, ``nrf54l15dk/nrf54l10/cpuapp``,
``nrf54l15dk/nrf54l15/cpuapp``, ``nrf54lm20dk/nrf54lm20a/cpuapp``,
``nrf54lm20dk/nrf54lm20b/cpuapp``, ``nrf7120dk/nrf7120/cpuapp``, ``nrf9280pdk/nrf9280/cpuapp``,
``opentitan_earlgrey``, ``ophelia4ev/nrf54l15/cpuapp``, ``pic32cm_gc00_cpro``,
``pic32cm_jh01_cpro``, ``pic32cm_sg00_cpro``, ``pic32cx_sg41_cult``, ``pic32cx_sg61_cult``,
``pic32cz_ca80_cult``, ``pic32cz_ca90_cult``, ``raytac_an54lq_db_15/nrf54l15/cpuapp``,
``raytac_an54lq_db_15/nrf54l15/cpuflpr``, ``raytac_an54lq_db_15/nrf54l15/cpuflpr/xip``,
``sam_e54_xpro``, ``siwx917_dk2605a``, ``siwx917_ek2708a``, ``siwx917_rb4338a``,
``siwx917_rb4342a``, ``xiao_nrf54l15/nrf54l15/cpuapp``, ``xiao_nrf54l15/nrf54l15/cpuflpr``,
``xmc45_relax_kit`` and ``xmc47_relax_kit``. Pull request CI builds it on ``mps2/an385``.

Some watchdog peripherals are covered by dedicated build variants that select an overlay
through ``DTC_OVERLAY_FILE`` or ``FILE_SUFFIX``. Each variant is restricted to the boards
listed in its ``platform_allow`` entry in :file:`tests.yaml`, and pull request CI builds it on
the board listed in its ``integration_platforms`` entry:

* STM32 window watchdog (WWDG): :file:`boards/stm32_wwdg.overlay`, and
  :file:`boards/stm32h7_wwdg.overlay` for STM32H7 boards.
* STM32 independent watchdog (IWDG): :file:`boards/stm32_iwdg.overlay`, optionally with
  :kconfig:option:`CONFIG_IWDG_STM32_EARLY_WAKEUP` enabled.
* GD32 free watchdog (FWDGT) and window watchdog (WWDGT): :file:`boards/gd32_fwdgt.overlay`
  and :file:`boards/gd32_wwdgt.overlay`.
* NXP S32 Software Watchdog Timer (SWT) on :zephyr:board:`s32z2xxdc2` and
  :zephyr:board:`s32k5xxcvb` (build only).
* nRF54H20 global watchdog (GSWDT) on :zephyr:board:`nrf54h20dk`, using
  ``FILE_SUFFIX=nrf_gswdt``.

Building and Running
********************

In this sample, a watchdog callback is used to handle a timeout event once. This functionality is used to request an action before the board
restarts due to a timeout event in the watchdog driver.

The watchdog peripheral is configured in the board's ``.dts`` file. Make sure that the watchdog is enabled
using the configuration file in ``boards`` folder.

Building and Running for ST Nucleo F091RC
=========================================

The sample can be built and executed for the
:zephyr:board:`nucleo_f091rc` as follows:

.. zephyr-app-commands::
	:zephyr-app: samples/drivers/watchdog
	:board: nucleo_f091rc
	:goals: build flash
	:compact:

To build for another board, change "nucleo_f091rc" to the name of that board and provide a corresponding devicetree overlay.

Sample output
=============

You should get a similar output as below:

.. code-block:: console

	Watchdog sample application
	Attempting to test pre-reset callback
	Feeding watchdog 5 times
	Feeding watchdog...
	Feeding watchdog...
	Feeding watchdog...
	Feeding watchdog...
	Feeding watchdog...
	Waiting for reset...
	Handled things..ready to reset

.. note:: After the last message, the board will reset and the sequence will start again
