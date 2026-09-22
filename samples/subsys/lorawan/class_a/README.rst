.. zephyr:code-sample:: lorawan-class-a
   :name: LoRaWAN class A device
   :relevant-api: lorawan_api

   Join a LoRaWAN network and send a message periodically.

Overview
********

A simple application to demonstrate the :ref:`LoRaWAN subsystem <lorawan_api>` of Zephyr.
Every fifth uplink requests a link check, logging the demodulation margin and
gateway count reported by the network.

Requirements
************

A board with a LoRa radio supported by the :ref:`LoRa driver API <lora_api>`, listing ``lora`` in
its supported features and exposing the radio through the ``lora0`` devicetree alias.

The sample has been tested on the following boards:

* :zephyr:board:`nucleo_wl55jc`, also with the native LoRa backend (STM32WL sub-GHz radio)
* :zephyr:board:`96b_wistrio`

Pull request CI builds it on :zephyr:board:`nucleo_wl55jc`.

Building and Running
********************

Before building the sample, make sure to select the correct region in the
``prj.conf`` file.

The following commands build and flash the sample.

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/lorawan/class_a
   :board: nucleo_wl55jc
   :goals: build flash
   :compact:

Important Notes for Multiple Runs
*********************************

By default, this example will only succeed the first time it is run. On subsequent join attempts, the LoRaWAN network server may reject the join request due to a hardcoded ``dev_nonce`` value. According to the LoRaWAN specification, ``dev_nonce`` must increment for every new connection attempt.

To run this sample multiple times, choose one of the following options:

1. **Manually Increment ``dev_nonce``:**
   Modify the sample code to increment ``join_cfg.otaa.dev_nonce`` before each connection attempt and ensure it is preserved across reboots.

2. **Built-in Zephyr Settings Implementation:**
   Enable :kconfig:option:`CONFIG_LORAWAN_NVM_SETTINGS` in the Kconfig. This allows proper storage and reuse of configuration settings, including the ``dev_nonce``, across multiple runs.
