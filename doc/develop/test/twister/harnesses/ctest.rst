.. _twister_ctest_harness:

Ctest Harness
#############

The ``ctest`` harness integrates CMake's CTest testing framework with Twister, allowing
you to leverage CTest's capabilities for organizing and running tests. This harness is
particularly useful for projects that already use CMake and CTest for their test infrastructure.

Overview
********

CTest is CMake's built-in testing tool that provides a standardized way to define, run,
and report test results. The ctest harness in Twister acts as a bridge, allowing CTest-based
tests to be executed and validated through Twister's infrastructure.

When using this harness, Twister delegates test execution to CTest while managing the build,
flash, and device interaction aspects. The test results from CTest are then collected and
reported by Twister.

Configuration
*************

The ctest harness is specified in the test configuration file:

.. code-block:: yaml

   tests:
     my.ctest.scenario:
       harness: ctest

Configuration Options
=====================

ctest_args: <list of arguments> (default empty)
    Specifies additional command-line arguments to pass to ``ctest`` during execution.
    This allows you to customize CTest's behavior per test scenario.
    
    Example:

    .. code-block:: yaml

        tests:
          my.ctest.retry:
            harness: ctest
            harness_config:
              ctest_args:
                - '--repeat until-pass:5'
                - '--verbose'

    Note: The ``--ctest-args`` option can be passed multiple times on the Twister
    command line to specify multiple arguments to CTest.

Usage Example
*************

Here's an example of a test configuration using the ctest harness:

.. code-block:: yaml

   common:
     tags: integration
   tests:
     integration.ctest.basic:
       harness: ctest
     integration.ctest.extended:
       harness: ctest
       harness_config:
         ctest_args:
           - '--repeat until-pass:3'
           - '--output-on-failure'

Common CTest Arguments
**********************

Some commonly used CTest arguments that can be specified via ``ctest_args``:

``--verbose`` or ``-V``
    Enable verbose output from tests

``--output-on-failure``
    Show test output only when tests fail

``--repeat until-pass:N``
    Repeat failing tests up to N times until they pass

``--repeat until-fail:N``
    Repeat tests up to N times until they fail (useful for detecting flaky tests)

``--repeat after-timeout:N``
    Repeat tests that timeout up to N times

``--timeout <seconds>``
    Set a timeout for each test

``--stop-on-failure``
    Stop running tests after the first failure

CMakeLists.txt Configuration
*****************************

To use the ctest harness, your test application's ``CMakeLists.txt`` needs to define
tests using CTest's ``add_test()`` command:

.. code-block:: cmake

   enable_testing()
   
   add_test(NAME test_basic
            COMMAND ${CMAKE_CURRENT_BINARY_DIR}/my_test_executable)
   
   add_test(NAME test_advanced
            COMMAND ${CMAKE_CURRENT_BINARY_DIR}/my_test_executable --advanced)

Use Cases
*********

The ctest harness is particularly useful when:

- You have existing CMake-based test infrastructure
- You want to leverage CTest's test management features
- You need to run multiple test executables or configurations
- You want fine-grained control over test execution and reporting

Best Practices
**************

1. **Organize tests logically**: Use CTest's labeling and grouping features to organize
   related tests.

2. **Set appropriate timeouts**: Use CTest's timeout features to prevent hung tests from
   blocking the test suite.

3. **Use descriptive test names**: Make test names clear and descriptive for easier
   debugging and reporting.

4. **Leverage CTest features**: Take advantage of CTest's built-in capabilities like
   test fixtures, resource allocation, and parallel execution where appropriate.

5. **Handle dependencies**: Use CTest's ``DEPENDS`` property to manage test dependencies
   correctly.

Limitations
***********

- The ctest harness requires proper CMake test configuration in your project
- Not all CTest features may be fully supported or applicable in embedded contexts
- Some CTest arguments may not be compatible with device testing workflows

See Also
********

- :ref:`twister_harnesses` - Overview of all available harnesses
- :ref:`twister_script` - Main Twister documentation
- CMake CTest documentation - For detailed information on CTest features and usage
