.. zephyr:code-sample:: tfm_config_build
   :name: TF-M Build Configuration Test
   :relevant-api: tfm_api

   Test TF-M builds in various configurations.

Overview
********

This sample tests building Zephyr applications with TF-M (Trusted Firmware-M)
in various configurations. It is primarily used for validating that different
TF-M build options work correctly.

The sample itself is minimal (just prints "Hello World"), but the purpose is
to verify that the build system can successfully build with different TF-M
configurations such as:

- With or without MCUboot BL2 (second stage bootloader)
- Single-image vs multi-image configurations
- Various platform-specific settings

This sample is mainly for testing and validation purposes rather than
demonstrating specific TF-M features.

Requirements
************

* A board with TF-M support (non-secure target variant)
* ARM TrustZone support

Building and Running
********************

Build the sample with different TF-M configurations:

Default configuration:

.. zephyr-app-commands::
   :zephyr-app: samples/tfm_integration/config_build
   :board: nrf5340dk/nrf5340/cpuapp/ns
   :goals: build
   :compact:

Without BL2:

.. zephyr-app-commands::
   :zephyr-app: samples/tfm_integration/config_build
   :board: nrf5340dk/nrf5340/cpuapp/ns
   :goals: build
   :gen-args: -DCONFIG_TFM_BL2=n
   :compact:

Sample Output
=============

.. code-block:: console

   Hello World! nrf5340dk/nrf5340/cpuapp/ns
