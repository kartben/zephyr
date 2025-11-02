.. _twister_harnesses:

Test Harnesses
##############

Overview
********

Twister harnesses are the mechanisms by which Twister executes tests and determines
whether they pass or fail. Each harness provides a different approach to test execution
and result validation, suited to different testing scenarios and requirements.

A harness acts as the bridge between your test code and Twister's test execution
infrastructure. It handles:

* Launching the test application
* Capturing and parsing test output
* Determining test success or failure
* Reporting results back to Twister

Choosing the Right Harness
***************************

Selecting the appropriate harness depends on your testing needs:

+-------------------+------------------------------------------+------------------------+
| Harness           | Best For                                 | Complexity             |
+===================+==========================================+========================+
| :ref:`ztest       | Zephyr kernel, drivers, subsystems       | Low                    |
| <twister_ztest    | (recommended for most Zephyr testing)    |                        |
| _harness>`        |                                          |                        |
+-------------------+------------------------------------------+------------------------+
| :ref:`console     | Pattern matching in console output       | Low-Medium             |
| <twister_console  | Custom output validation                 |                        |
| _harness>`        |                                          |                        |
+-------------------+------------------------------------------+------------------------+
| :ref:`pytest      | Complex test logic in Python             | Medium                 |
| <twister_pytest   | External tool integration                |                        |
| _harness>`        |                                          |                        |
+-------------------+------------------------------------------+------------------------+
| :ref:`shell       | Shell command testing                    | Low-Medium             |
| <twister_shell    | Command-line interface validation        |                        |
| _harness>`        |                                          |                        |
+-------------------+------------------------------------------+------------------------+
| :ref:`robot       | Interactive scenarios in Renode          | Medium                 |
| <twister_robot    | Human-readable test specifications       |                        |
| _harness>`        |                                          |                        |
+-------------------+------------------------------------------+------------------------+
| :ref:`gtest       | C++ testing with Google Test framework   | Medium                 |
| <twister_gtest    | Existing gtest codebases                 |                        |
| _harness>`        |                                          |                        |
+-------------------+------------------------------------------+------------------------+
| :ref:`ctest       | CMake-based test management              | Low-Medium             |
| <twister_ctest    | Integration with CTest infrastructure    |                        |
| _harness>`        |                                          |                        |
+-------------------+------------------------------------------+------------------------+
| :ref:`power       | Power consumption measurement            | Medium-High            |
| <twister_power    | Battery life validation                  |                        |
| _harness>`        |                                          |                        |
+-------------------+------------------------------------------+------------------------+
| :ref:`bsim        | BabbleSim Bluetooth simulation           | Low                    |
| <twister_bsim     | Multi-device Bluetooth scenarios         |                        |
| _harness>`        |                                          |                        |
+-------------------+------------------------------------------+------------------------+
| :ref:`test        | Simple pass/fail with success string     | Very Low               |
| <twister_test     | Minimal testing needs                    |                        |
| _harness>`        |                                          |                        |
+-------------------+------------------------------------------+------------------------+

Quick Start Guide
*****************

For New Zephyr Tests
====================

If you're writing new tests for Zephyr code, start with **ztest**:

.. code-block:: yaml

    tests:
      subsys.my_feature.basic:
        harness: ztest
        tags: my_feature

.. code-block:: c

    #include <zephyr/ztest.h>

    ZTEST(my_suite, test_basic_functionality) {
        zassert_equal(1 + 1, 2, "Math works");
    }

    ZTEST_SUITE(my_suite, NULL, NULL, NULL, NULL, NULL);

For Shell Testing
=================

To test shell commands, use the **shell** harness:

.. code-block:: yaml

    tests:
      shell.test.commands:
        harness: shell
        harness_config:
          shell_commands:
            - command: "help"
              expected: "Available commands:"
            - command: "kernel version"
              expected: "Zephyr version"

For Python-Based Testing
=========================

When you need complex test logic in Python, use **pytest**:

.. code-block:: yaml

    tests:
      integration.test.complex:
        harness: pytest
        harness_config:
          pytest_root:
            - pytest/test_integration.py

For Custom Output Validation
=============================

To validate specific patterns in output, use **console**:

.. code-block:: yaml

    tests:
      sensor.test.reading:
        harness: console
        harness_config:
          type: multi_line
          regex:
            - "Temperature: [0-9]+\\.[0-9]+ C"
            - "Humidity: [0-9]+\\.[0-9]+ %"

Common Harness Features
***********************

Output Parsing
==============

All harnesses that parse output (ztest, gtest, console, pytest, etc.) operate on
the serial console output from the device under test. They look for specific patterns,
markers, or structured output to determine test results.

Timeout Handling
================

Harnesses respect the timeout configured for test scenarios. If a test doesn't
complete within the specified timeout, it's marked as failed. See
:ref:`twister_test_case_timeout` for timeout configuration.

Result Reporting
================

All harnesses report results in a standardized format that Twister understands:

* **PASS**: Test completed successfully
* **FAIL**: Test completed but failed assertions or checks
* **ERROR**: Test encountered an error during execution
* **SKIP**: Test was skipped (e.g., missing fixture, platform filter)

See :ref:`twister_statuses` for detailed information about test statuses.

Harness Configuration
*********************

Harnesses are configured in the test's YAML file using the ``harness`` and
``harness_config`` keywords:

.. code-block:: yaml

    tests:
      example.test:
        harness: <harness_name>
        harness_config:
          # Harness-specific configuration options
          option1: value1
          option2: value2

Each harness has its own set of configuration options documented in its respective
page.

Multiple Harnesses in One Test Suite
*************************************

Different test scenarios within the same test suite can use different harnesses:

.. code-block:: yaml

    common:
      tags: comprehensive
    tests:
      basic.test:
        harness: ztest
      shell.test:
        harness: shell
        harness_config:
          shell_commands:
            - command: "help"
              expected: "Available commands:"
      integration.test:
        harness: pytest
        harness_config:
          pytest_root:
            - pytest/test_integration.py

This allows you to use the most appropriate harness for each specific test scenario.

Harness Limitations and Compatibility
**************************************

Platform Support
================

Not all harnesses work on all platforms:

* **ztest**, **console**, **test**: Work on all platforms
* **pytest**, **shell**: Require console/shell access
* **robot**: Requires Renode simulation
* **bsim**: Requires native simulation platform and BabbleSim
* **power**: Requires specific power monitoring hardware
* **gtest**: Requires C++ support
* **ctest**: Requires CMake test registration

Hardware vs. Simulation
========================

Some harnesses are better suited for specific environments:

* **Simulation-only**: bsim, robot
* **Hardware-preferred**: power
* **Either**: ztest, console, pytest, shell, gtest, ctest, test

Unsupported Harnesses
*********************

Some harness names are placeholders for future implementation:

* **keyboard**: For tests requiring keyboard interaction (not implemented)
* **net**: For network testing scenarios (not implemented)
* **bluetooth**: For Bluetooth-specific testing (not implemented)

If a test specifies an unimplemented harness, Twister will skip it and report
that the harness is not available.

Detailed Harness Documentation
*******************************

.. toctree::
   :maxdepth: 1

   harnesses/ztest
   harnesses/console
   harnesses/pytest
   harnesses/shell
   harnesses/robot
   harnesses/gtest
   harnesses/ctest
   harnesses/power
   harnesses/bsim
   harnesses/test

Best Practices
**************

1. **Choose the simplest harness that meets your needs**: Start with ztest for most
   Zephyr testing. Only use more complex harnesses when necessary.

2. **Be consistent within a test suite**: Use the same harness for related tests
   when possible to maintain consistency.

3. **Document harness choice**: If using a non-standard harness, comment in the
   YAML file explaining why it was chosen.

4. **Consider maintenance**: More complex harnesses may require more maintenance
   as test requirements evolve.

5. **Test harness configuration**: Verify that harness configuration works as
   expected before writing extensive tests.

6. **Handle edge cases**: Ensure your harness configuration handles error cases,
   timeouts, and unexpected output.

7. **Review harness output**: Regularly check test logs to ensure harnesses are
   correctly interpreting results.

Advanced Topics
***************

Custom Harnesses
================

For specialized testing needs not covered by existing harnesses, you can create
custom harnesses. This requires:

1. Implementing a Python class that inherits from Twister's base harness class
2. Implementing methods for test execution and result parsing
3. Registering the harness with Twister
4. Documenting the harness for other users

Custom harnesses should be considered only when existing harnesses cannot meet
your requirements.

Harness Selection Algorithm
============================

When Twister runs, it:

1. Reads the test configuration YAML file
2. Identifies the harness specified (or uses default 'ztest')
3. Validates that the harness is available
4. Configures the harness with the provided ``harness_config`` options
5. Executes the test using the harness
6. Collects results from the harness
7. Reports results in Twister's output

Additional Resources
********************

* :ref:`Test Configuration <test_config_args>` - General test configuration options
* :ref:`Twister Statuses <twister_statuses>` - Understanding test result statuses
* :ref:`Test Framework <test-framework>` - Ztest framework documentation
* :ref:`Pytest Integration <integration_with_pytest>` - Detailed pytest information
