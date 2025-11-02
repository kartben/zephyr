.. _twister_index:

Twister Documentation
#####################

Overview
********

Twister is Zephyr's comprehensive testing tool that automates building, running, and
reporting on tests across multiple platforms. This guide covers all aspects of using
Twister for testing Zephyr applications.

Getting Started
***************

New to Twister? Start here:

1. Read the :ref:`main Twister guide <twister_script>` for an introduction
2. Learn about :ref:`test configuration <test_config_args>` to write your own tests
3. Explore :ref:`test harnesses <twister_harnesses>` to understand how tests are validated
4. Check :ref:`test statuses <twister_statuses>` to interpret test results

Key Documentation
*****************

Core Concepts
=============

- :ref:`twister_script` - Main Twister documentation covering basic usage,
  command-line options, and general concepts
- :ref:`Test Configuration <test_config_args>` - How to configure tests in
  ``testcase.yaml`` and ``sample.yaml`` files
- :ref:`twister_statuses` - Understanding test execution statuses and what they mean

Test Harnesses
==============

Test harnesses determine how Twister evaluates test results. Choose the right harness
for your testing needs:

- :ref:`twister_harness_ztest` - Zephyr's native test framework (recommended for most tests)
- :ref:`twister_harness_console` - Regex-based output validation for custom tests
- :ref:`twister_harness_pytest` - Python-based integration testing
- :doc:`All harnesses <harness>` - Complete guide to all available harnesses

Advanced Topics
===============

- :ref:`Running Tests on Hardware <running_tests_on_hardware>` - Device testing,
  hardware maps, and fixtures
- :ref:`twister_blackbox` - Black-box testing with Twister
- :ref:`Test Configuration Files <test_configuration>` - Advanced test configuration
- **Platform Scope** - Selecting which platforms to test
- **Integration Mode** - Running tests in CI/CD environments

Quick Reference
***************

Common Commands
===============

Run all tests on default platforms:

.. code-block:: bash

    ./scripts/twister

Run tests for a specific platform:

.. code-block:: bash

    ./scripts/twister -p qemu_x86

Run tests from a specific directory:

.. code-block:: bash

    ./scripts/twister -T tests/kernel

Run a specific test:

.. code-block:: bash

    ./scripts/twister -s tests/kernel/common

Run on real hardware:

.. code-block:: bash

    ./scripts/twister --device-testing --device-serial /dev/ttyACM0 -p board_name

Common Configuration Options
============================

Basic test configuration in ``testcase.yaml``:

.. code-block:: yaml

    tests:
      my.test.scenario:
        harness: ztest
        tags: kernel
        platform_allow: qemu_x86 native_sim
        min_ram: 16
        timeout: 30

For more details, see :ref:`test configuration options <test_config_args>`.

Documentation Index
*******************

.. toctree::
   :maxdepth: 2

   Main Twister Guide <../twister>
   Test Statuses <../twister_statuses>
   Test Harnesses <harness>
   Black-box Testing <twister_blackbox>

Related Documentation
*********************

- :ref:`Ztest Framework <test-framework>` - Zephyr's unit testing framework
- :ref:`Pytest Integration <integration_with_pytest>` - Using pytest with Zephyr
- :ref:`BabbleSim Testing <bsim>` - Bluetooth simulation testing
- :ref:`Code Coverage <coverage>` - Measuring test coverage

Contributing
************

When writing new tests:

1. Choose the appropriate test harness for your use case
2. Follow existing test naming conventions
3. Tag tests appropriately for easy filtering
4. Set realistic timeout values
5. Document any special requirements or fixtures
6. Test on multiple platforms when possible

For questions or issues, refer to the Zephyr project documentation or community channels.
