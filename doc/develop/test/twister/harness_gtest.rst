.. _twister_harness_gtest:

Gtest Harness
#############

Overview
********

The Gtest harness provides integration between Twister and the Google Test (gTest) framework.
This harness is particularly useful when you have existing tests written using the gTest
framework and want to run them as part of Zephyr's testing infrastructure without rewriting
them to use Ztest.

Like the Ztest harness, the Gtest harness monitors console output and parses messages
emitted by the gTest framework to determine test results. It recognizes gTest's standard
output format including test case names, assertion results, and summary information.

When to Use Gtest
*****************

Consider using the Gtest harness when:

- You have existing gTest-based tests from another project that you want to integrate
- Your team is more familiar with gTest conventions and syntax
- You need compatibility with existing gTest tooling or infrastructure
- You want to maintain consistency with tests in related projects that use gTest

However, for new Zephyr-specific tests, the :ref:`Ztest harness <twister_harness_ztest>`
is recommended as it's designed specifically for the Zephyr environment and provides
better integration with Zephyr's testing infrastructure.

How It Works
************

The Gtest harness operates by:

1. **Output Monitoring**: Capturing console output from the running test
2. **Pattern Recognition**: Identifying gTest framework output patterns
3. **Result Parsing**: Extracting test case results from formatted output
4. **Status Reporting**: Communicating pass/fail status back to Twister

The harness recognizes standard gTest output including:

- Test case start markers (``[ RUN      ]``)
- Test case completion markers (``[       OK ]`` or ``[  FAILED  ]``)
- Assertion failure messages
- Final test summary statistics

Configuration
*************

To use the Gtest harness, specify it in your test configuration:

.. code-block:: yaml

    tests:
      myproject.unit.gtest_example:
        harness: gtest
        tags: unit_test

No additional ``harness_config`` options are typically required for basic usage.

Integration with Zephyr
***********************

While gTest is a C++ testing framework and Zephyr is primarily C-based, you can use
gTest in Zephyr projects that include C++ components. Your test code will need to:

1. Include the gTest headers
2. Define test cases using gTest macros (``TEST``, ``TEST_F``, etc.)
3. Initialize the gTest framework in your ``main()`` function
4. Build with C++ support enabled in your test configuration

Example Test Configuration
**************************

Here's a complete example of a test using the Gtest harness:

**testcase.yaml**:

.. code-block:: yaml

    common:
      tags: unit_test cpp
    tests:
      mylib.unit.math_functions:
        harness: gtest
        build_only: false
        tags: unit_test

**CMakeLists.txt**:

.. code-block:: cmake

    cmake_minimum_required(VERSION 3.20.0)
    find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
    project(gtest_example)
    
    # Enable C++ support
    enable_language(CXX)
    
    target_sources(app PRIVATE src/test_math.cpp)

**src/test_math.cpp**:

.. code-block:: cpp

    #include <gtest/gtest.h>
    
    // Simple function to test
    int add(int a, int b) {
        return a + b;
    }
    
    // Test case using gTest
    TEST(MathTests, TestAddition) {
        EXPECT_EQ(add(2, 3), 5);
        EXPECT_EQ(add(-1, 1), 0);
        EXPECT_EQ(add(0, 0), 0);
    }
    
    TEST(MathTests, TestAdditionCommutative) {
        EXPECT_EQ(add(5, 7), add(7, 5));
    }
    
    int main() {
        testing::InitGoogleTest();
        return RUN_ALL_TESTS();
    }

Output Format
*************

When a gTest test runs, it produces output like this:

.. code-block:: none

    [==========] Running 2 tests from 1 test suite.
    [----------] Global test environment set-up.
    [----------] 2 tests from MathTests
    [ RUN      ] MathTests.TestAddition
    [       OK ] MathTests.TestAddition (0 ms)
    [ RUN      ] MathTests.TestAdditionCommutative
    [       OK ] MathTests.TestAdditionCommutative (0 ms)
    [----------] 2 tests from MathTests (0 ms total)
    
    [----------] Global test environment tear-down
    [==========] 2 tests from 1 test suite ran. (0 ms total)
    [  PASSED  ] 2 tests.

The Gtest harness parses this output to determine that both tests passed.

Best Practices
**************

When using the Gtest harness:

1. **Use Descriptive Test Names**: Follow gTest naming conventions with clear test
   suite and test case names (``TEST(SuiteName, TestName)``).

2. **Keep Tests Independent**: Each test should be self-contained. Use test fixtures
   (``TEST_F``) when you need shared setup/teardown logic.

3. **Choose Appropriate Assertions**: gTest provides ``ASSERT_*`` (fatal) and
   ``EXPECT_*`` (non-fatal) macros. Use ``EXPECT_*`` when you want to report
   multiple failures in a single test.

4. **Minimize Dependencies**: Avoid depending on external C++ libraries that may
   not be readily available in the Zephyr build environment.

5. **Consider Ztest for New Tests**: For new Zephyr-specific tests, consider using
   the native :ref:`Ztest framework <twister_harness_ztest>` instead.

Limitations
***********

When using gTest with Zephyr and Twister:

- **C++ Support Required**: Your test must be built with C++ support enabled
- **Memory Overhead**: gTest has more memory overhead than Ztest, which may be
  a concern on resource-constrained platforms
- **Limited Examples**: There are fewer examples of gTest usage in the Zephyr
  tree compared to Ztest
- **Build Complexity**: C++ tests may require additional build configuration

Troubleshooting
***************

Common Issues
=============

**Build Failures**
    If your test fails to build, verify that:
    
    - C++ is enabled in your CMakeLists.txt (``enable_language(CXX)``)
    - gTest headers are available in your build environment
    - Your toolchain supports C++ for the target platform

**Test Not Detected**
    Ensure that:
    
    - The test output is being captured correctly
    - The gTest framework is initialized (``testing::InitGoogleTest()``)
    - ``RUN_ALL_TESTS()`` is called in your main function

**Timeout Issues**
    gTest tests may take longer to execute due to C++ runtime overhead. Consider
    increasing the ``timeout`` value in your testcase.yaml if needed.

See Also
********

- `Google Test Documentation <https://google.github.io/googletest/>`_
- :ref:`Ztest Harness <twister_harness_ztest>`
- :ref:`Test Configuration <test_config_args>`
