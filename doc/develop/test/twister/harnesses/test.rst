.. _twister_test_harness:

Test Harness
############

Overview
********

The ``test`` harness is a simple, generic harness that validates test execution by
checking for a specific success string in the test output. It provides a straightforward
way to verify that a test has completed successfully without requiring a full testing
framework.

This harness is useful for simple tests or applications that produce a known success
indicator but don't use a structured testing framework like ztest or gtest.

When to Use Test Harness
=========================

Use the test harness when:

* You have a simple test that outputs a specific success message
* You don't need a full testing framework's structure
* You're testing standalone applications or samples
* The test produces a clear, identifiable success indicator
* You want minimal overhead and complexity

How It Works
============

The test harness looks for a predefined success string in the test output:

* Success string: ``"PROJECT EXECUTION SUCCESSFUL"``

If this string is found in the test output, the test is considered to have passed.
Otherwise, it fails.

Configuration
*************

Basic Configuration
===================

To use the test harness, specify it in your test configuration:

.. code-block:: yaml

    tests:
      sample.test.simple:
        harness: test
        tags: sample

Application Code
================

Your test application must output the success string when it completes successfully:

.. code-block:: c

    #include <zephyr/kernel.h>
    #include <stdio.h>

    void main(void)
    {
        // Your test logic here
        
        // Indicate success
        printf("PROJECT EXECUTION SUCCESSFUL\n");
    }

Best Practices
**************

1. **Use for simple scenarios**: The test harness is best for straightforward tests
   with a clear success indicator.

2. **Consider ztest for complex tests**: For tests with multiple test cases or complex
   assertions, use the ztest harness instead.

3. **Output the success string last**: Ensure your code outputs the success string
   only after all test logic has completed successfully.

4. **Handle errors before success**: Don't output the success string if any part of
   the test fails.

Limitations
***********

Be aware of these limitations:

1. **Single pass/fail**: No support for multiple test cases or partial failures.
2. **Fixed success string**: Cannot customize the success indicator.
3. **No assertion support**: Cannot use structured assertions like in ztest.
4. **Limited reporting**: Only reports overall pass/fail, not detailed results.

For more sophisticated testing needs, consider using :ref:`twister_ztest_harness` instead.
