.. zephyr:code-sample:: ble_peripheral_hr
   :name: Heart-rate Monitor (Peripheral)
   :relevant-api: bt_hrs bt_bas bluetooth

   Expose a Heart Rate (HR) GATT Service generating dummy heart-rate values.

Overview
********

Similar to the :zephyr:code-sample:`ble_peripheral` sample, except that this
application specifically exposes the HR (Heart Rate) GATT Service. Once a device
connects it will generate dummy heart-rate values.


Requirements
************

* BlueZ running on the host, or
* A board with Bluetooth LE support

The sample is known to work on the following boards:

* :zephyr:board:`qemu_cortex_m3` and :zephyr:board:`qemu_x86`, using BlueZ on the host
* :zephyr:board:`bbc_microbit`, with the minimal configuration
* :zephyr:board:`nrf51dk`, :zephyr:board:`nrf52dk`, :zephyr:board:`nrf52840dk`,
  :zephyr:board:`nrf5340dk`, :zephyr:board:`nrf54l15dk`, :zephyr:board:`nrf54lm20dk` and
  :zephyr:board:`ophelia4ev`
* :zephyr:board:`rv32m1_vega`
* :zephyr:board:`frdm_k64f`, :zephyr:board:`mimxrt1020_evk`, :zephyr:board:`mimxrt1050_evk`
  and :zephyr:board:`mimxrt1060_evk`, with the :ref:`frdm_kw41z_shield` as the controller

The sample also runs on the :ref:`nrf52_bsim`, :ref:`nrf5340bsim` and :ref:`nrf54l15bsim`
simulated boards.

Building and Running
********************

Building a minimal variant
--------------------------

.. zephyr-app-commands::
   :zephyr-app: samples/bluetooth/peripheral_hr
   :board: qemu_cortex_m3
   :goals: build
   :gen-args: -DCONF_FILE=prj_minimal.conf

Building a minimal variant for bbc_microbit
-------------------------------------------

.. zephyr-app-commands::
   :zephyr-app: samples/bluetooth/peripheral_hr
   :board: bbc_microbit
   :goals: build
   :gen-args: -DCONF_FILE=prj_minimal.conf -DEXTRA_CONF_FILE=overlay-bt_ll_sw_split-minimal.conf
