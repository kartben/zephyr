.. _twister_harness_ztest:

Ztest Harness
#############

Overview
********

The Ztest harness is Twister's primary integration with Zephyr's native testing framework,
:ref:`Ztest <test-framework>`. This harness is the most commonly used in Zephyr's test
suite and provides comprehensive support for unit testing, integration testing, and
functional testing within the Zephyr RTOS environment.

When you use the Ztest harness, Twister monitors the test's console output looking for
special formatted messages that the Ztest framework emits during test execution. These
messages include test case start/end markers, assertion results, and final pass/fail
verdicts.

How It Works
************

The Ztest harness operates by:

1. **Test Discovery**: Recognizing Ztest framework output patterns in the console
2. **Progress Tracking**: Monitoring individual test case execution as they run
3. **Result Collection**: Capturing pass/fail status for each test case
4. **Summary Generation**: Aggregating results and determining overall test suite success

The harness looks for specific output frames that Ztest generates, including:

- Test suite initialization messages
- Individual test case start markers
- Assertion pass/fail indicators
- Test case completion markers
- Final test suite summary

Configuration
*************

Using the Ztest harness is straightforward - simply specify it in your test configuration:

.. code-block:: yaml

    tests:
      kernel.threads.test:
        harness: ztest
        tags: kernel threads

No additional ``harness_config`` is typically required for basic Ztest usage.

Advanced Configuration
**********************

The Ztest harness supports several advanced configuration options through ``harness_config``:

Test Repetition
===============

You can configure tests to run multiple times using these options:

.. code-block:: yaml

    tests:
      my.stress.test:
        harness: ztest
        harness_config:
          ztest_suite_repeat: 5
          ztest_test_repeat: 3
          ztest_test_shuffle: true

**ztest_suite_repeat: <int>** (default 1)
    The number of times the entire test suite should be repeated. This is useful for
    stress testing and identifying intermittent failures.

**ztest_test_repeat: <int>** (default 1)
    The number of times each individual test case within the suite should be repeated.
    Use this to verify test case stability and reproducibility.

**ztest_test_shuffle: <True|False>** (default False)
    When enabled, the order of test cases within the suite will be randomized. This
    helps identify hidden dependencies between test cases. See also
    :ref:`Running Tests in Random Order <ztest_shuffle>`.

Best Practices
**************

When using the Ztest harness:

1. **Use Descriptive Test Names**: Ztest test case names should clearly describe what
   is being tested. The full test case identifier follows the pattern:
   ``<test_scenario>.<ztest_suite>.<ztest_test>``

2. **Keep Tests Independent**: Each test case should be self-contained and not depend
   on the execution order or state from previous tests. Use setup and teardown
   functions appropriately.

3. **Use Appropriate Assertions**: Ztest provides various assertion macros
   (``zassert_true``, ``zassert_equal``, etc.). Choose the most specific assertion
   for better failure diagnostics.

4. **Minimize Console Output**: While debugging output can be helpful, excessive
   console logging can slow down test execution and make logs harder to read.

5. **Test One Thing**: Each test case should verify a single behavior or aspect.
   Multiple assertions in a test are fine if they're all related to the same behavior.

Example Test
************

Here's a complete example of a test using the Ztest harness:

**testcase.yaml**:

.. code-block:: yaml

    common:
      tags: kernel timer
    tests:
      kernel.timer.basic:
        harness: ztest
        min_ram: 16
        tags: kernel

**src/main.c**:

.. code-block:: c

    #include <zephyr/ztest.h>
    #include <zephyr/kernel.h>

    ZTEST_SUITE(timer_tests, NULL, NULL, NULL, NULL, NULL);

    ZTEST(timer_tests, test_timer_duration)
    {
        k_timeout_t duration = K_MSEC(100);
        uint64_t start_ms = k_uptime_get();
        
        k_sleep(duration);
        
        uint64_t elapsed_ms = k_uptime_get() - start_ms;
        zassert_true(elapsed_ms >= 100, "Timer expired too early");
        zassert_true(elapsed_ms < 150, "Timer expired too late");
    }

    ZTEST(timer_tests, test_timer_cancel)
    {
        struct k_timer my_timer;
        k_timer_init(&my_timer, NULL, NULL);
        
        k_timer_start(&my_timer, K_SECONDS(10), K_NO_WAIT);
        k_timer_stop(&my_timer);
        
        zassert_equal(k_timer_status_get(&my_timer), 0,
                     "Timer should be stopped");
    }

When this test runs, the Ztest harness will:

1. Detect the test suite initialization
2. Monitor execution of ``test_timer_duration`` and ``test_timer_cancel``
3. Capture assertion results
4. Report final test status to Twister

Troubleshooting
***************

Common Issues
=============

**Test Not Detected**
    Ensure your test file includes the Ztest framework headers and properly defines
    test suites and cases using the ``ZTEST_SUITE`` and ``ZTEST`` macros.

**Timeout Failures**
    If tests are timing out, check if:
    
    - The test is stuck in an infinite loop
    - The test requires more time than the default timeout (60 seconds)
    - You need to adjust the ``timeout`` value in your testcase.yaml

**Assertion Failures Not Reported**
    Verify that:
    
    - Console output is being captured correctly
    - The test binary is actually executing (not just building)
    - Serial baud rate is configured correctly for hardware testing

See Also
********

- :ref:`Ztest Framework Documentation <test-framework>`
- :ref:`Test Configuration <test_config_args>`
- :ref:`Twister Console Harness <twister_harness_console>`
- :ref:`Running Tests in Random Order <ztest_shuffle>`
