.. zephyr:code-sample:: dtdoctor_demo
   :name: DT Doctor Demo
   :relevant-api: gpio_interface

   Demonstrate the dtdoctor tool for diagnosing Devicetree-related build errors.

Overview
********

This sample demonstrates the ``dtdoctor`` static analysis tool that helps diagnose
Devicetree-related build errors.

``dtdoctor`` intercepts error messages from the compiler and linker and, when they
refer to unresolved Devicetree device symbols (e.g. ``__device_dts_ord_*``), provides
detailed information about what might be causing the error and how to fix it.

The sample includes scenarios that trigger different types of Devicetree errors that
``dtdoctor`` can help diagnose:

1. **Disabled devicetree nodes** - Nodes that are disabled but referenced in code
2. **Missing driver Kconfig** - Enabled nodes without the required driver Kconfig options

Building and Running
********************

Basic Build
===========

To build the sample normally (without triggering errors):

.. zephyr-app-commands::
   :zephyr-app: samples/dtdoctor_demo
   :board: reel_board
   :goals: build
   :compact:

This basic build should succeed on most boards that have an LED (``led0`` alias).

Using dtdoctor to Diagnose Errors
==================================

The real value of this demo is in demonstrating how ``dtdoctor`` helps diagnose
Devicetree-related build errors.

Scenario 1: Disabled Devicetree Node
-------------------------------------

To demonstrate ``dtdoctor`` diagnosing a disabled devicetree node:

1. Create a board overlay file that defines but disables a sensor:

   .. code-block:: devicetree

      / {
          aliases {
              demo-sensor = &demo_sensor;
          };
      };

      &i2c0 {
          demo_sensor: sensor@40 {
              compatible = "bosch,bme280";
              reg = <0x40>;
              status = "disabled";
          };
      };

2. Modify the source code to unconditionally reference the sensor (remove the
   ``#if DT_NODE_HAS_STATUS`` guards).

3. Build with dtdoctor enabled:

   .. code-block:: shell

      west build -b reel_board samples/dtdoctor_demo -- -DZEPHYR_SCA_VARIANT=dtdoctor

4. ``dtdoctor`` will detect the error and provide diagnostic information explaining
   that the node is disabled and how to enable it.

Scenario 2: Missing Driver Kconfig
-----------------------------------

To demonstrate ``dtdoctor`` diagnosing missing Kconfig options:

1. Create a board overlay that defines and enables a sensor:

   .. code-block:: devicetree

      / {
          aliases {
              demo-sensor = &demo_sensor;
          };
      };

      &i2c0 {
          demo_sensor: sensor@40 {
              compatible = "bosch,bme280";
              reg = <0x40>;
              status = "okay";
          };
      };

2. Build **without** enabling the required sensor driver Kconfig option
   (``CONFIG_BME280`` should not be set).

3. Build with dtdoctor enabled:

   .. code-block:: shell

      west build -b reel_board samples/dtdoctor_demo -- -DZEPHYR_SCA_VARIANT=dtdoctor

4. ``dtdoctor`` will detect the linker error and suggest enabling the missing
   Kconfig options.

Understanding the Output
=========================

When ``dtdoctor`` detects a Devicetree-related error, it displays a formatted
diagnostic table with:

- The devicetree node that's causing the issue
- The current status of the node
- What other nodes depend on it
- Which chosen/alias references point to it
- Suggested fixes (e.g., Kconfig options to enable)

Example dtdoctor output for a disabled node:

.. code-block:: none

   +-------------------------------------------------------------------+
   | DT Doctor                                                         |
   +===================================================================+
   | 'demo_sensor: /soc/i2c@40003000/sensor@40' is disabled in        |
   | boards/reel_board.overlay:5                                       |
   |                                                                   |
   | It is referenced as a "chosen" in 'demo-sensor'                  |
   |                                                                   |
   | Try enabling the node by setting its 'status' property to        |
   | 'okay'.                                                           |
   +-------------------------------------------------------------------+

Example dtdoctor output for missing driver Kconfig:

.. code-block:: none

   +-------------------------------------------------------------------+
   | DT Doctor                                                         |
   +===================================================================+
   | 'demo_sensor: /soc/i2c@40003000/sensor@40' is enabled but no     |
   | driver appears to be available for it.                           |
   |                                                                   |
   | Try enabling these Kconfig options:                              |
   |  - CONFIG_BME280=y                                               |
   |  - CONFIG_I2C=y                                                  |
   +-------------------------------------------------------------------+

Requirements
************

This demo works on any board that has:

- An LED connected via GPIO (``led0`` alias)

For demonstrating the error scenarios, you'll need to create appropriate board
overlays as described above.

Tips
****

- ``dtdoctor`` is most useful when you encounter cryptic build errors involving
  ``__device_dts_ord_*`` symbols.

- The tool automatically activates when building with
  ``-DZEPHYR_SCA_VARIANT=dtdoctor`` and intercepts compiler/linker errors.

- You can also use the underlying ``dtdoctor_analyzer.py`` script directly for
  post-mortem analysis of build failures.

See Also
********

- :ref:`dtdoctor` - Full dtdoctor documentation
- :ref:`devicetree-guide` - Devicetree guide
- :ref:`set-devicetree-overlays` - How to use devicetree overlays
