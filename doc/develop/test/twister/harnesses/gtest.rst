.. _twister_gtest_harness:

Gtest Harness
#############

Overview
********

The ``gtest`` harness enables integration of Google Test (gTest) framework tests
with Twister. Google Test is a widely-used C++ testing framework that provides a
rich set of assertions, test organization features, and output formatting.

Use the gtest harness if you have existing tests written in the gTest framework
and do not wish to update them to zTest, or if you prefer gTest's C++-oriented
features and assertion style for new tests.

When to Use Gtest Harness
==========================

Use the gtest harness when:

* You have existing gTest-based tests that you want to run with Twister
* You prefer gTest's C++ testing features over zTest
* Your test code is primarily in C++ and benefits from gTest's C++ features
* You want to use gTest's rich assertion macros and matchers
* You're migrating tests from other projects that use Google Test
* You need gTest-specific features like death tests, type-parameterized tests, or value-parameterized tests

The gtest harness is particularly useful for:

* C++ component testing
* Testing C++ APIs and classes
* Complex assertion scenarios that benefit from gTest's matchers
* Projects with existing gTest test suites
* Testing code that uses C++ features extensively

How It Works
============

The gtest harness operates by:

1. **Building the test**: Twister builds the test application which includes the
   gTest framework and your test code

2. **Executing tests**: The built test executable runs, executing all registered
   gTest test cases

3. **Parsing output**: The harness parses gTest's output format, looking for
   pass/fail indicators, test results, and assertions

4. **Reporting results**: Test outcomes are collected and reported through Twister's
   standard reporting mechanisms

The gtest harness understands gTest's output format and can correctly interpret
test results, failures, and diagnostic messages.

Understanding Gtest Output Parsing
===================================

The gtest harness recognizes gTest's standardized output format:

* ``[ RUN      ]`` - Test is starting
* ``[       OK ]`` - Test passed
* ``[  FAILED  ]`` - Test failed
* ``[==========]`` - Test summary lines
* Assertion failures with file, line, and message information

This allows Twister to accurately track which tests pass or fail and provide
detailed failure information.

Configuration
*************

Basic Configuration
===================

To use the gtest harness, specify it in your test configuration:

.. code-block:: yaml

    tests:
      component.test.gtest:
        harness: gtest
        tags: unit_test cpp

This minimal configuration runs all gTest tests in your application.

Platform Requirements
=====================

The gtest harness typically requires:

* C++ compiler support
* Sufficient ROM/RAM for the gTest framework
* Platform support for C++ features used by gTest

.. code-block:: yaml

    tests:
      cpp.component.test:
        harness: gtest
        tags: cpp
        filter: CONFIG_CPLUSPLUS
        min_flash: 64
        min_ram: 32

Example Configurations
**********************

Basic Gtest Test
================

Simple gtest harness configuration:

.. code-block:: yaml

    tests:
      unit.gtest.basic:
        harness: gtest
        tags: unit_test
        filter: CONFIG_CPLUSPLUS

With C++ Requirements
=====================

Ensure C++ is available and configured:

.. code-block:: yaml

    tests:
      component.gtest.cpp:
        harness: gtest
        tags: cpp unit_test
        filter: CONFIG_CPLUSPLUS and CONFIG_NEWLIB_LIBC
        extra_configs:
          - CONFIG_CPLUSPLUS=y

Platform-Specific Tests
=======================

Run only on platforms with adequate resources:

.. code-block:: yaml

    tests:
      complex.gtest.test:
        harness: gtest
        tags: cpp complex
        min_flash: 128
        min_ram: 64
        filter: CONFIG_CPLUSPLUS

Writing Gtest Tests
*******************

Including Google Test
=====================

Include the gTest headers in your test file:

.. code-block:: cpp

    #include <gtest/gtest.h>

Basic Test Structure
====================

Write tests using gTest's ``TEST()`` macro:

.. code-block:: cpp

    #include <gtest/gtest.h>

    // Simple test case
    TEST(MathTest, Addition) {
        EXPECT_EQ(2 + 2, 4);
        EXPECT_NE(2 + 2, 5);
    }

    TEST(MathTest, Multiplication) {
        EXPECT_EQ(3 * 4, 12);
        EXPECT_GT(10 * 5, 40);
    }

Test Fixtures
=============

Use test fixtures for setup and teardown:

.. code-block:: cpp

    #include <gtest/gtest.h>

    class QueueTest : public ::testing::Test {
    protected:
        void SetUp() override {
            // Setup code here
            queue.clear();
        }

        void TearDown() override {
            // Cleanup code here
        }

        Queue queue;
    };

    TEST_F(QueueTest, IsEmptyInitially) {
        EXPECT_EQ(queue.size(), 0);
    }

    TEST_F(QueueTest, DequeueWorks) {
        queue.enqueue(1);
        queue.enqueue(2);
        EXPECT_EQ(queue.dequeue(), 1);
    }

Assertions
==========

Gtest provides rich assertion macros:

.. code-block:: cpp

    // Basic assertions
    EXPECT_TRUE(condition);
    EXPECT_FALSE(condition);
    EXPECT_EQ(expected, actual);
    EXPECT_NE(val1, val2);

    // Comparison assertions
    EXPECT_LT(val1, val2);    // Less than
    EXPECT_LE(val1, val2);    // Less than or equal
    EXPECT_GT(val1, val2);    // Greater than
    EXPECT_GE(val1, val2);    // Greater than or equal

    // String assertions
    EXPECT_STREQ(str1, str2);
    EXPECT_STRNE(str1, str2);

    // Floating-point assertions
    EXPECT_FLOAT_EQ(expected, actual);
    EXPECT_DOUBLE_EQ(expected, actual);
    EXPECT_NEAR(val1, val2, abs_error);

Using ASSERT vs EXPECT
======================

* ``EXPECT_*`` - Test continues after failure
* ``ASSERT_*`` - Test stops immediately on failure

.. code-block:: cpp

    TEST(Example, AssertVsExpect) {
        EXPECT_EQ(1, 1);  // Continues if fails
        EXPECT_EQ(2, 2);  // Still runs

        ASSERT_NE(ptr, nullptr);  // Stops if fails
        ptr->doSomething();       // Only runs if assertion passes
    }

Main Function
=============

You typically need a minimal main function for gTest:

.. code-block:: cpp

    #include <gtest/gtest.h>

    int main(int argc, char **argv) {
        ::testing::InitGoogleTest(&argc, argv);
        return RUN_ALL_TESTS();
    }

Advanced Features
*****************

Parameterized Tests
===================

Test the same code with different inputs:

.. code-block:: cpp

    #include <gtest/gtest.h>

    class PrimeTest : public ::testing::TestWithParam<int> {
    };

    TEST_P(PrimeTest, IsPrime) {
        EXPECT_TRUE(isPrime(GetParam()));
    }

    INSTANTIATE_TEST_SUITE_P(
        PrimeNumbers,
        PrimeTest,
        ::testing::Values(2, 3, 5, 7, 11, 13)
    );

Death Tests
===========

Test that code terminates in expected ways:

.. code-block:: cpp

    TEST(DeathTest, AssertionFailure) {
        ASSERT_DEATH({
            ASSERT_TRUE(false);
        }, "Assertion.*failed");
    }

Typed Tests
===========

Test generic code with different types:

.. code-block:: cpp

    template <typename T>
    class StackTest : public ::testing::Test {
    };

    typedef ::testing::Types<int, float, double> StackTypes;
    TYPED_TEST_SUITE(StackTest, StackTypes);

    TYPED_TEST(StackTest, PushPop) {
        Stack<TypeParam> stack;
        stack.push(TypeParam(1));
        EXPECT_EQ(stack.pop(), TypeParam(1));
    }

Best Practices
**************

1. **Use descriptive test names**: Test names should clearly indicate what is being
   tested and under what conditions.

   .. code-block:: cpp

       // Good
       TEST(StringUtils, ToUpperConvertsLowercaseLetters)

       // Less clear
       TEST(StringUtils, Test1)

2. **One assertion concept per test**: Keep tests focused on a single concept or behavior.

3. **Use test fixtures for common setup**: Don't repeat setup code across tests.

4. **Prefer EXPECT over ASSERT**: Use ``EXPECT_*`` unless you need to abort the test
   immediately on failure.

5. **Use appropriate assertion macros**: Choose the most specific assertion for
   better failure messages.

   .. code-block:: cpp

       // Better
       EXPECT_EQ(actual, expected);

       // Less informative failure message
       EXPECT_TRUE(actual == expected);

6. **Test edge cases**: Include tests for boundary conditions, empty inputs, and
   error cases.

7. **Keep tests independent**: Each test should be able to run independently in any order.

8. **Use meaningful failure messages**: Add custom messages to assertions when helpful.

   .. code-block:: cpp

       EXPECT_GT(size, 0) << "Container should not be empty after insertion";

9. **Organize tests logically**: Group related tests in the same test case.

10. **Clean up resources**: Use ``TearDown()`` to ensure resources are properly released.

Running Gtest Tests
*******************

Running with Twister
====================

Execute gtest-based tests through Twister:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         $ ./scripts/twister -p native_sim -T tests/unit/component

   .. group-tab:: Windows

      .. code-block:: bat

         python .\scripts\twister -p native_sim -T tests\unit\component

Running Tests Manually
=======================

For debugging, you can run the test executable directly:

.. code-block:: bash

    $ cd build_dir
    $ ./zephyr/zephyr.exe

Gtest Command-Line Options
===========================

Gtest supports various command-line options. While the Twister harness doesn't
directly support passing these, you can modify your test's main() to use them
during development:

.. code-block:: cpp

    int main(int argc, char **argv) {
        ::testing::InitGoogleTest(&argc, argv);
        
        // During development, you can hard-code options:
        // ::testing::GTEST_FLAG(filter) = "MathTest.*";
        // ::testing::GTEST_FLAG(repeat) = 10;
        
        return RUN_ALL_TESTS();
    }

Common gTest command-line options:
* ``--gtest_filter=<pattern>`` - Run specific tests
* ``--gtest_repeat=<count>`` - Repeat tests
* ``--gtest_shuffle`` - Randomize test order
* ``--gtest_color=<yes|no|auto>`` - Control colored output

Debugging Failed Tests
**********************

When gtest tests fail:

1. **Read the failure output**: Gtest provides detailed failure messages including
   file, line, and actual vs. expected values.

2. **Run specific tests**: Modify the filter in main() to run only failing tests.

3. **Use a debugger**: Run the test executable in a debugger and set breakpoints
   at test code or assertions.

4. **Add debug output**: Use ``std::cout`` or ``SCOPED_TRACE`` to add diagnostic output.

5. **Check for setup issues**: Verify ``SetUp()`` is correctly initializing test state.

6. **Review test order**: If tests fail only when run together, check for unwanted
   inter-test dependencies.

7. **Increase logging**: Gtest output can be verbose; review all output for clues.

Common Issues and Solutions
****************************

**Issue**: Tests compile but don't run
    **Solution**: Ensure ``main()`` function calls ``RUN_ALL_TESTS()``. Verify the
    executable is built and accessible.

**Issue**: ASSERT causes unexpected termination
    **Solution**: This is expected behavior. Use ``EXPECT`` if you want tests to
    continue after failure.

**Issue**: Floating-point comparisons fail unexpectedly
    **Solution**: Use ``EXPECT_FLOAT_EQ`` or ``EXPECT_NEAR`` instead of ``EXPECT_EQ``
    for floating-point values.

**Issue**: Out of memory
    **Solution**: Gtest framework requires significant resources. Increase ``min_ram``
    and ``min_flash`` in test configuration or simplify tests.

**Issue**: C++ exceptions not working
    **Solution**: Ensure platform supports exceptions or disable death tests and
    exception-based features.

**Issue**: Tests fail on embedded platform but pass on native
    **Solution**: Check for platform-specific behavior, resource constraints, or
    differences in C++ standard library implementation.

Comparison with Ztest
**********************

+------------------+-------------------------+-------------------------+
| Feature          | Gtest                   | Ztest                   |
+==================+=========================+=========================+
| Language         | C++                     | C                       |
+------------------+-------------------------+-------------------------+
| Assertions       | Very rich (100+ macros) | Focused set             |
+------------------+-------------------------+-------------------------+
| Fixtures         | Class-based             | Function-based          |
+------------------+-------------------------+-------------------------+
| Parameterization | Built-in support        | Limited                 |
+------------------+-------------------------+-------------------------+
| Memory footprint | Larger                  | Smaller                 |
+------------------+-------------------------+-------------------------+
| Learning curve   | Moderate                | Low                     |
+------------------+-------------------------+-------------------------+
| Zephyr-specific  | No                      | Yes                     |
+------------------+-------------------------+-------------------------+

Choose gtest when:
* You need C++ testing features
* You have existing gtest tests
* You want rich assertion capabilities

Choose ztest when:
* Testing in C
* Minimizing footprint is important
* You want Zephyr-specific features

Limitations
***********

Be aware of these limitations:

1. **C++ requirement**: Gtest requires C++ support, which not all platforms provide.

2. **Resource overhead**: Gtest framework is larger than zTest, requiring more ROM/RAM.

3. **Platform compatibility**: Some gtest features (like exceptions, RTTI) may not
   work on all embedded platforms.

4. **Limited Zephyr integration**: Gtest is not aware of Zephyr-specific features
   like kernel objects or device tree.

5. **Toolchain dependency**: Requires a C++ capable toolchain.

Additional Resources
********************

* `Google Test Documentation <https://google.github.io/googletest/>`_
* `Google Test Primer <https://google.github.io/googletest/primer.html>`_
* `Google Test Advanced Guide <https://google.github.io/googletest/advanced.html>`_
* `Google Test FAQ <https://google.github.io/googletest/faq.html>`_
* :ref:`Ztest Documentation <test-framework>` (for comparison)
