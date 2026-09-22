.. zephyr:code-sample:: alarm
   :name: Counter Alarm
   :relevant-api: counter_interface

   Implement an alarm application using the counter API.

Overview
********
This sample provides an example of alarm application using :ref:`counter API <counter_api>`.
It sets an alarm with an initial delay of 2 seconds. At each alarm
expiry, a new alarm is configured with a delay multiplied by 2.

.. note::
   In case of 1Hz frequency (RTC for example), precision is 1 second.
   Therefore, the sample output may differ in 1 second

Requirements
************

This sample requires the support of a timer IP compatible with alarm setting.

The counter instance is selected per driver in :file:`src/main.c`, and the board must enable it in
devicetree. Boards and SoCs with an overlay in the sample's :file:`boards` or :file:`socs`
directories provide this.

The ``sample.drivers.counter.alarm`` scenario is tested on the following boards:

- Nordic: ``nrf51dk/nrf51822``, ``nrf52dk/nrf52832``, ``nrf52840dk/nrf52840``,
  ``nrf5340dk/nrf5340/cpuapp``, ``nrf54h20dk/nrf54h20/cpuapp``, ``nrf54h20dk/nrf54h20/cpuflpr``,
  ``nrf54h20dk/nrf54h20/cpuppr``, ``nrf54h20dk/nrf54h20/cpurad``, ``nrf54l15dk/nrf54l15/cpuapp``,
  ``nrf54l15dk/nrf54l15/cpuflpr``, ``nrf54lm20dk/nrf54lm20a/cpuapp``,
  ``nrf54lm20dk/nrf54lm20b/cpuapp``, ``nrf7120dk/nrf7120/cpuapp``, ``nrf9160dk/nrf9160`` and
  ``bl5340_dvk/nrf5340/cpuapp``
- STM32: ``nucleo_f746zg``, ``stm32h735g_disco`` and ``stm32h573i_dk``
- GD32: ``gd32e103v_eval``, ``gd32e507z_eval``, ``gd32f403z_eval``, ``gd32f450i_eval``,
  ``gd32f450z_eval``, ``gd32e507v_start``, ``gd32f407v_start``, ``gd32f450v_start`` and
  ``gd32f470i_eval``
- Microchip: ``sama7d65_curiosity``, ``sama7g54_ek``, ``samd20_xpro``, ``mec172xevb_assy6906``,
  ``mec15xxevb_assy6853`` and ``mec_assy6941/mec1753_qsz``
- NXP: ``mr_canhubk3``, ``s32z2xxdc2/s32z270/rtu0``, ``s32z2xxdc2/s32z270/rtu1``,
  ``s32z2xxdc2@D/s32z270/rtu0`` and ``s32z2xxdc2@D/s32z270/rtu1``
- Raspberry Pi: ``rpi_pico``
- Silicon Labs: ``slwrb4180b``, ``xg24_rb4187c``, ``xg27_rb4194a`` and ``xg29_rb4412a``
- TI: ``lp_em_cc2340r5``

On STM32, the ``sample.drivers.counter.alarm.stm32_rtc`` scenario uses the RTC instead of a
general-purpose timer. It is tested on ``disco_l475_iot1``, ``nucleo_f746zg`` and
``stm32l562e_dk/stm32l562xx/ns``.

Pull request CI builds both scenarios on ``nucleo_f746zg`` only.

References
**********

- :zephyr:board:`disco_l475_iot1`

Building and Running
********************

 .. zephyr-app-commands::
    :zephyr-app: samples/drivers/counter/alarm
    :host-os: unix
    :board: disco_l475_iot1
    :goals: run
    :compact:

Sample Output
=============

 .. code-block:: console

    Counter alarm sample

    Set alarm in 2 sec
    !!! Alarm !!!
    Now: 2
    Set alarm in 4 sec
    !!! Alarm !!!
    Now: 6
    Set alarm in 8 sec
    !!! Alarm !!!
    Now: 14
    Set alarm in 16 sec
    !!! Alarm !!!
    Now: 30

    <repeats endlessly>
