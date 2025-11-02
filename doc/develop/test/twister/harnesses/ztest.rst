.. _twister_ztest_harness:

Ztest Harness
#############

Overview
********

The ``ztest`` harness is the native Zephyr testing framework harness. It is specifically
designed for Zephyr and provides deep integration with Zephyr's kernel, drivers, and
subsystems. Ztest is the recommended harness for most Zephyr testing scenarios.

The ztest harness parses the structured output produced by tests written using the Ztest
framework (``<zephyr/ztest.h>``), automatically detecting test results, assertions, and
test case execution.

When to Use Ztest Harness
==========================

Use the ztest harness when:

* Writing new tests for Zephyr code
* Testing Zephyr kernel features, drivers, or subsystems
* You want Zephyr-specific testing features
* Minimizing test framework overhead is important
* You're writing tests in C (zTest is C-based)
* You need integration with Zephyr's device tree and Kconfig systems

The ztest harness is ideal for:

* Kernel testing
* Driver testing
* Subsystem testing
* Integration testing within Zephyr
* Testing on resource-constrained platforms

For detailed information on writing Ztest tests, see :ref:`test-framework`.

Configuration
*************

Basic Configuration
===================

To use the ztest harness, specify it in your test configuration:

.. code-block:: yaml

    tests:
      kernel.test.example:
        harness: ztest
        tags: kernel

This is the default harness, so it can often be omitted:

.. code-block:: yaml

    tests:
      kernel.test.example:
        tags: kernel

Key Features
************

The ztest harness understands Ztest's structured output format and automatically
detects:

* Test suite start and completion
* Individual test case execution
* Test assertions (pass/fail)
* Skip conditions
* Test setup and teardown
* Detailed failure information

For complete documentation on the Ztest framework itself, refer to :ref:`test-framework`.

Additional Resources
********************

* :ref:`Ztest Framework Documentation <test-framework>`
* :ref:`Writing Zephyr Tests <testing>`
