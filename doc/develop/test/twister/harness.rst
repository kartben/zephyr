.. _twister_harnesses:

Test Harnesses
##############

Overview
********

Harnesses are a critical component of Twister's testing infrastructure that define how test
execution is monitored and how success or failure is determined. A harness acts as the
bridge between the test execution environment (whether simulated or on real hardware) and
Twister's test result analysis system.

When you specify a harness in your test's ``testcase.yaml`` or ``sample.yaml`` file,
you're instructing Twister on how to interpret the test's output and determine whether
the test has passed or failed. Different harnesses are designed for different testing
methodologies and frameworks.

Understanding Harness Types
****************************

Twister supports several types of harnesses, each designed for specific testing scenarios:

**Output Parsing Harnesses**
  These harnesses (``ztest``, ``gtest``, ``console``) monitor the test's console output
  and look for specific patterns or messages that indicate test results. They parse text
  output in real-time during test execution.

**Integration Harnesses**
  Harnesses like ``pytest``, ``robot``, and ``shell`` integrate with external test
  frameworks, leveraging their native capabilities for test execution and reporting.

**Specialized Harnesses**
  Some harnesses serve specific purposes, such as ``power`` for power consumption
  measurements, ``bsim`` for BabbleSim simulation, and ``ctest`` for CMake-based tests.

Selecting the Right Harness
****************************

The choice of harness depends on your testing needs:

- Use :ref:`ztest <twister_harness_ztest>` for Zephyr's native test framework (most common)
- Use :ref:`gtest <twister_harness_gtest>` for Google Test-based tests
- Use :ref:`console <twister_harness_console>` for custom output parsing with regex
- Use :ref:`pytest <twister_harness_pytest>` for Python-based integration tests
- Use :ref:`robot <twister_harness_robot>` for Robot Framework tests in Renode
- Use :ref:`power <twister_harness_power>` for power consumption validation
- Use :ref:`shell <twister_harness_shell>` for shell command-based testing
- Use :ref:`bsim <twister_harness_bsim>` for BabbleSim simulation scenarios
- Use :ref:`ctest <twister_harness_ctest>` for CMake test integration

Common Harness Configuration
*****************************

Harnesses are specified in your test configuration file using the ``harness`` keyword:

.. code-block:: yaml

    tests:
      my.test.scenario:
        harness: console
        harness_config:
          type: multi_line
          regex:
            - "Test completed successfully"

Many harnesses support additional configuration through the ``harness_config`` section,
allowing you to customize their behavior for your specific test requirements.

Unsupported Harnesses
*********************

Some widely used harness types are not yet supported in Twister:

- keyboard
- net
- bluetooth

If your test requires one of these harness types, consider using an alternative approach
or contributing an implementation to the Twister project.

Example Configuration
*********************

Here's a complete example showing harness configuration for a sensor test:

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

This configuration uses the console harness to look for specific temperature and humidity
readings in the test output, validating that the sensor is working correctly.

Available Harnesses
*******************

.. toctree::
   :maxdepth: 1

   harness_ztest
   harness_gtest
   harness_console
   harness_pytest
   harness_robot
   harness_power
   harness_shell
   harness_bsim
   harness_ctest
