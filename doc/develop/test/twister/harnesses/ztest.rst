.. _twister_ztest_harness:

Ztest Harness
#############

The ``ztest`` harness is one of the most commonly used harnesses in Twister. It is designed
to work with Zephyr's :ref:`Ztest testing framework <ztest>`, which provides a structured
way to write and organize unit tests in Zephyr applications.

Overview
********

The Ztest harness parses the test application's output to identify test results reported by
the Ztest framework. It looks for specific pass/fail frames that the Ztest framework outputs
during test execution. This makes it ideal for automated testing of Zephyr components and
applications that use the Ztest API.

How It Works
************

When a test application built with Ztest runs, it outputs structured information about:

- Test suite execution
- Individual test case results
- Assertions and their outcomes
- Summary statistics

The Ztest harness parses this output to determine:

- Which test cases passed or failed
- Total number of tests run
- Any assertion failures or errors
- Overall test suite status

Configuration
*************

The Ztest harness is specified simply in the test's ``testcase.yaml``:

.. code-block:: yaml

   tests:
     kernel.semaphore:
       harness: ztest
       tags: kernel

No additional ``harness_config`` section is typically required for basic Ztest usage, as
the harness automatically recognizes the standard Ztest output format.

Additional Configuration Options
=================================

The Ztest harness supports additional configuration through the ``harness_config`` section:

ztest_suite_repeat: <int> (default 1)
    Specifies the number of times the entire test suite should be repeated.

ztest_test_repeat: <int> (default 1)
    Specifies the number of times each individual test within the test suite
    should be repeated.

ztest_test_shuffle: <True|False> (default False)
    Indicates whether the order of the tests within the test suite should
    be shuffled. When set to ``true``, tests are executed in random order.

Example with configuration:

.. code-block:: yaml

   tests:
     kernel.common.test:
       harness: ztest
       harness_config:
         ztest_suite_repeat: 3
         ztest_test_repeat: 2
         ztest_test_shuffle: true

Usage Example
*************

Here's a complete example of a test using the Ztest harness:

.. code-block:: yaml

   common:
     tags: kernel
   tests:
     kernel.scheduler:
       harness: ztest
     kernel.scheduler.multiq:
       harness: ztest
       extra_configs:
         - CONFIG_SCHED_MULTIQ=y
     kernel.scheduler.dumb:
       harness: ztest
       extra_configs:
         - CONFIG_SCHED_DUMB=y

Best Practices
**************

When using the Ztest harness:

1. **Use Ztest API**: Ensure your test code uses the Ztest framework's macros and functions
   (``zassert_*``, ``ZTEST()``, etc.)

2. **Clear Test Names**: Use descriptive names for test suites and test cases to make
   debugging easier

3. **Proper Assertions**: Use appropriate Ztest assertions to validate test conditions

4. **Test Independence**: Design tests to be independent of each other to avoid cascading
   failures

5. **Resource Cleanup**: Ensure tests properly clean up resources to avoid affecting
   subsequent tests

See Also
********

- :ref:`ztest` - Main Ztest framework documentation
- :ref:`twister_harnesses` - Overview of all available harnesses
- :ref:`twister_script` - Main Twister documentation
