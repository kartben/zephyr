.. _twister_harnesses:

Harnesses
#########

Harnesses ``ztest``, ``gtest`` and ``console`` are based on parsing of the
output and matching certain phrases. ``ztest`` and ``gtest`` harnesses look
for pass/fail/etc. frames defined in those frameworks.

Some widely used harnesses that are not supported yet:

- keyboard
- net
- bluetooth

The following is an example yaml file with a few harness_config options.

.. code-block:: yaml

      sample:
        name: HTS221 Temperature and Humidity Monitor
      common:
        tags: sensor
        harness: console
        harness_config:
          type: multi_line
          ordered: false
          regex:
            - "Temperature:(.*)C"
            - "Relative Humidity:(.*)%"
          fixture: i2c_hts221
      tests:
        test:
          tags: sensors
          depends_on: i2c

Available Harnesses
*******************

.. toctree::
   :maxdepth: 1

   harness_ctest
   harness_gtest
   harness_pytest
   harness_console
   harness_robot
   harness_power
   harness_bsim
   harness_shell
