.. _twister_gtest_harness:

Gtest Harness
#############

The ``gtest`` harness enables Twister to work with tests written using Google's GoogleTest
(gtest) C++ testing framework. This harness is useful when you have existing gtest-based
tests that you want to run in the Zephyr environment without converting them to Ztest.

Overview
********

The gtest harness parses the output from GoogleTest framework to identify test results.
Like the Ztest harness, it looks for specific pass/fail frames that gtest outputs during
test execution, but follows the gtest output format instead.

This harness provides an alternative for projects or developers who:

- Have existing test suites written in gtest
- Prefer the gtest API and conventions
- Are migrating tests from other platforms that use gtest
- Want to leverage gtest-specific features

How It Works
************

The gtest harness monitors the console output from the test application and recognizes
the standard GoogleTest output format, including:

- Test case and test suite execution messages
- Pass/fail indicators for individual tests
- Assertion failure details
- Summary statistics and results

Configuration
*************

The gtest harness requires minimal configuration:

.. code-block:: yaml

   tests:
     my.gtest.scenario:
       harness: gtest
       tags: unit_test

No additional ``harness_config`` options are typically required, as the harness automatically
recognizes the standard gtest output format.

Usage Example
*************

Here's a basic example using the gtest harness:

.. code-block:: yaml

   common:
     tags: cpp unit_test
   tests:
     cpp.gtest.basic:
       harness: gtest
     cpp.gtest.advanced:
       harness: gtest
       extra_configs:
         - CONFIG_CPP_EXCEPTIONS=y

When to Use Gtest vs Ztest
***************************

**Use gtest when:**

- You already have gtest-based tests from another project
- You're more familiar with gtest's API and conventions  
- You need gtest-specific features like type-parameterized tests
- You're working with C++ code and prefer C++ testing idioms

**Use Ztest when:**

- Writing new Zephyr-specific tests
- Working primarily with C code
- You want native Zephyr integration and conventions
- You need features specific to embedded testing in Zephyr

Requirements
************

To use the gtest harness, your test application must:

1. Link against the GoogleTest library
2. Include appropriate gtest headers
3. Use gtest macros and assertions (``TEST()``, ``ASSERT_*``, ``EXPECT_*``, etc.)
4. Call ``RUN_ALL_TESTS()`` in your main function

Best Practices
**************

1. **Keep tests independent**: Ensure each test case can run independently without
   depending on the state from other tests.

2. **Use descriptive names**: Follow gtest conventions for naming test suites and test
   cases clearly.

3. **Choose appropriate assertions**: Use ``ASSERT_*`` for conditions that should stop
   the test immediately, and ``EXPECT_*`` for conditions where the test should continue.

4. **Clean up resources**: Use gtest's fixtures (test classes) to ensure proper setup
   and teardown of resources.

Limitations
***********

- The gtest framework and harness support in Zephyr may have some limitations compared
  to desktop environments
- Memory-constrained platforms may struggle with gtest's overhead
- Some advanced gtest features may not be fully supported

See Also
********

- :ref:`twister_ztest_harness` - Native Zephyr test framework harness
- :ref:`twister_harnesses` - Overview of all available harnesses  
- :ref:`twister_script` - Main Twister documentation
