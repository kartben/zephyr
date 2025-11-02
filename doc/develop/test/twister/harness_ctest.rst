.. _twister_harness_ctest:

Ctest Harness
#############

Overview
********

The Ctest harness provides integration between Twister and CMake's built-in testing
tool, CTest. CTest is part of the CMake suite and provides a standardized way to
run tests that are defined in CMake build files.

This harness is useful when:

- You have existing CMake-based tests using ``add_test()``
- You want to leverage CTest's features like test dependencies and properties
- You're integrating external test suites that use CTest
- You need compatibility with CMake testing workflows

The Ctest harness allows you to run CMake/CTest-defined tests through Twister's
infrastructure while maintaining compatibility with standalone CTest execution.

How It Works
************

The Ctest harness workflow:

1. **Build Phase**: Twister builds the test application using CMake, which
   registers tests using ``add_test()``

2. **Test Discovery**: CTest discovers registered tests in the build directory

3. **Execution**: The Ctest harness invokes CTest to run the registered tests

4. **Result Collection**: Test results from CTest are collected and reported
   back to Twister

5. **Status Reporting**: Pass/fail status is determined based on CTest's exit code
   and output

This integration allows you to use CMake's native testing capabilities while
benefiting from Twister's platform management, filtering, and reporting features.

Configuration
*************

The Ctest harness is configured in your testcase.yaml:

.. code-block:: yaml

    tests:
      my.cmake.test:
        harness: ctest
        tags: cmake

Additional CTest Arguments
==========================

**ctest_args: <list of arguments>** (default empty)
    Specify additional command-line arguments to pass to CTest.
    
    Common options include:
    
    - ``--repeat until-pass:N``: Retry failing tests up to N times
    - ``--timeout <seconds>``: Set per-test timeout
    - ``--verbose``: Enable verbose output
    - ``--output-on-failure``: Show test output only for failures
    - ``--parallel <N>``: Run tests in parallel

    The ``--ctest-args`` option can be specified multiple times to pass
    several arguments to CTest.

Basic Example
*************

Simple CTest Integration
========================

**testcase.yaml**:

.. code-block:: yaml

    tests:
      test.cmake.basic:
        harness: ctest
        tags: cmake unit_test

**CMakeLists.txt**:

.. code-block:: cmake

    cmake_minimum_required(VERSION 3.20.0)
    find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
    project(ctest_example)
    
    target_sources(app PRIVATE src/main.c)
    
    # Enable testing
    enable_testing()
    
    # Add a test that runs the built application
    add_test(NAME my_test
             COMMAND ${CMAKE_BINARY_DIR}/zephyr/zephyr.exe)

This registers a simple test that CTest will execute.

With CTest Arguments
====================

**testcase.yaml**:

.. code-block:: yaml

    tests:
      test.cmake.retry:
        harness: ctest
        harness_config:
          ctest_args:
            - '--repeat until-pass:5'
            - '--output-on-failure'

This configuration:

- Retries failing tests up to 5 times
- Only shows output from tests that fail

Multiple Tests in CMake
========================

You can define multiple tests in your CMakeLists.txt:

**CMakeLists.txt**:

.. code-block:: cmake

    enable_testing()
    
    # Unit test 1
    add_test(NAME test_initialization
             COMMAND ${CMAKE_BINARY_DIR}/zephyr/zephyr.exe --test=init)
    
    # Unit test 2
    add_test(NAME test_runtime
             COMMAND ${CMAKE_BINARY_DIR}/zephyr/zephyr.exe --test=runtime)
    
    # Set test properties
    set_tests_properties(test_initialization PROPERTIES
                         TIMEOUT 30
                         DEPENDS build_dependencies)
    
    set_tests_properties(test_runtime PROPERTIES
                         TIMEOUT 60
                         DEPENDS test_initialization)

CTest will run both tests, respecting dependencies and timeouts.

Advanced CTest Features
***********************

Test Properties
===============

CTest supports various test properties that can be set in CMakeLists.txt:

.. code-block:: cmake

    enable_testing()
    
    add_test(NAME my_test COMMAND ${APP_BINARY})
    
    # Set test properties
    set_tests_properties(my_test PROPERTIES
        TIMEOUT 120                    # Maximum execution time
        WILL_FAIL FALSE                # Test is expected to pass
        PASS_REGULAR_EXPRESSION "SUCCESS"  # Pattern indicating pass
        FAIL_REGULAR_EXPRESSION "ERROR"    # Pattern indicating failure
        DEPENDS other_test             # Run after other_test
        LABELS "integration;hardware"  # Categorize test
    )

Test Fixtures
=============

CTest supports test fixtures for setup/cleanup:

.. code-block:: cmake

    enable_testing()
    
    # Setup fixture
    add_test(NAME setup_database
             COMMAND setup_db.sh)
    set_tests_properties(setup_database PROPERTIES
        FIXTURES_SETUP database)
    
    # Test requiring fixture
    add_test(NAME test_db_query
             COMMAND ${APP_BINARY})
    set_tests_properties(test_db_query PROPERTIES
        FIXTURES_REQUIRED database)
    
    # Cleanup fixture
    add_test(NAME cleanup_database
             COMMAND cleanup_db.sh)
    set_tests_properties(cleanup_database PROPERTIES
        FIXTURES_CLEANUP database)

CTest ensures setup runs before tests and cleanup runs after.

Passing Arguments to Tests
===========================

You can pass arguments to your test executable:

.. code-block:: cmake

    add_test(NAME test_with_args
             COMMAND ${APP_BINARY} --mode=stress --iterations=1000)

Resource Locks
==============

Prevent parallel execution of tests that share resources:

.. code-block:: cmake

    add_test(NAME test_serial_port_1 COMMAND ${APP_BINARY})
    add_test(NAME test_serial_port_2 COMMAND ${APP_BINARY})
    
    # Both tests use the same serial port - don't run in parallel
    set_tests_properties(test_serial_port_1 test_serial_port_2 PROPERTIES
        RESOURCE_LOCK serial_port_0)

CTest Command-Line Options
***************************

Useful CTest options you can pass via ``ctest_args``:

Test Selection
==============

.. code-block:: yaml

    ctest_args:
      - '-R test_pattern'         # Run tests matching regex
      - '-E exclude_pattern'      # Exclude tests matching regex
      - '-L label'                # Run tests with specific label
      - '-LE label'               # Exclude tests with label

Execution Control
=================

.. code-block:: yaml

    ctest_args:
      - '--parallel 4'            # Run 4 tests in parallel
      - '--stop-on-failure'       # Stop after first failure
      - '--timeout 300'           # Per-test timeout (seconds)

Output Control
==============

.. code-block:: yaml

    ctest_args:
      - '--verbose'               # Verbose output
      - '--output-on-failure'     # Show output only for failures
      - '--quiet'                 # Minimal output

Retry and Repeat
================

.. code-block:: yaml

    ctest_args:
      - '--repeat until-pass:5'   # Retry failures up to 5 times
      - '--repeat until-fail:10'  # Run until failure, max 10 times
      - '--repeat after-timeout:3' # Retry timeouts up to 3 times

Complete Example
****************

Here's a comprehensive example showing CTest harness features:

**testcase.yaml**:

.. code-block:: yaml

    common:
      tags: cmake integration
    tests:
      integration.ctest.full:
        harness: ctest
        min_ram: 64
        timeout: 300
        harness_config:
          ctest_args:
            - '--verbose'
            - '--output-on-failure'
            - '--repeat until-pass:3'
            - '--parallel 2'

**CMakeLists.txt**:

.. code-block:: cmake

    cmake_minimum_required(VERSION 3.20.0)
    find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
    project(ctest_integration)
    
    target_sources(app PRIVATE src/main.c src/tests.c)
    
    enable_testing()
    
    # Quick smoke tests
    add_test(NAME smoke_test_boot
             COMMAND ${CMAKE_BINARY_DIR}/zephyr/zephyr.exe --test=boot)
    set_tests_properties(smoke_test_boot PROPERTIES
        LABELS "smoke"
        TIMEOUT 30)
    
    # Integration tests
    add_test(NAME integration_test_i2c
             COMMAND ${CMAKE_BINARY_DIR}/zephyr/zephyr.exe --test=i2c)
    set_tests_properties(integration_test_i2c PROPERTIES
        LABELS "integration"
        TIMEOUT 60
        DEPENDS smoke_test_boot)
    
    add_test(NAME integration_test_spi
             COMMAND ${CMAKE_BINARY_DIR}/zephyr/zephyr.exe --test=spi)
    set_tests_properties(integration_test_spi PROPERTIES
        LABELS "integration"
        TIMEOUT 60
        DEPENDS smoke_test_boot)
    
    # Resource-locked test
    add_test(NAME exclusive_peripheral_test
             COMMAND ${CMAKE_BINARY_DIR}/zephyr/zephyr.exe --test=exclusive)
    set_tests_properties(exclusive_peripheral_test PROPERTIES
        RESOURCE_LOCK peripheral_device
        TIMEOUT 90)

Best Practices
**************

1. **Use Test Labels**: Organize tests with labels for easy selection:

   .. code-block:: cmake

       set_tests_properties(my_test PROPERTIES LABELS "smoke;quick")

2. **Set Appropriate Timeouts**: Configure realistic timeouts for each test:

   .. code-block:: cmake

       set_tests_properties(long_test PROPERTIES TIMEOUT 300)

3. **Document Dependencies**: Clearly document test dependencies and execution order

4. **Use Fixtures for Setup/Teardown**: Leverage CTest fixtures rather than
   implementing custom setup logic

5. **Parallel-Safe Tests**: Design tests to run safely in parallel when possible

6. **Meaningful Test Names**: Use descriptive test names that indicate what's
   being tested:

   .. code-block:: cmake

       add_test(NAME test_i2c_sensor_initialization COMMAND ...)

7. **Regular Expression Validation**: Use ``PASS_REGULAR_EXPRESSION`` to
   validate success:

   .. code-block:: cmake

       set_tests_properties(my_test PROPERTIES
           PASS_REGULAR_EXPRESSION "All tests passed")

Troubleshooting
***************

Common Issues
=============

**No Tests Found**
    - Ensure ``enable_testing()`` is called in CMakeLists.txt
    - Verify ``add_test()`` commands are present
    - Check that tests are registered before build completes
    - Run ``ctest -N`` in build directory to list available tests

**CTest Command Not Found**
    - Ensure CMake is installed and in PATH
    - Verify CMake version is compatible (3.20.0+)

**Tests Time Out**
    - Increase timeout in test properties or via ``--timeout`` argument
    - Check if test is actually completing
    - Review test output for hanging operations

**Tests Don't Run in Expected Order**
    - Use ``DEPENDS`` property to specify dependencies
    - Remember CTest runs tests in parallel by default with ``--parallel``
    - Use ``RESOURCE_LOCK`` for tests that can't run concurrently

**Output Not Visible**
    - Add ``--verbose`` to ``ctest_args``
    - Use ``--output-on-failure`` to see only failed test output
    - Check CTest log files in the build directory

Standalone CTest Usage
**********************

You can also run CTest directly in the build directory:

.. code-block:: bash

    # List available tests
    cd build_directory
    ctest -N
    
    # Run all tests
    ctest
    
    # Run specific test
    ctest -R test_name
    
    # Run with verbose output
    ctest -V
    
    # Run tests in parallel
    ctest -j4

This is useful for debugging CTest configuration outside of Twister.

See Also
********

- `CTest Documentation <https://cmake.org/cmake/help/latest/manual/ctest.1.html>`_
- `CMake add_test <https://cmake.org/cmake/help/latest/command/add_test.html>`_
- `CMake set_tests_properties <https://cmake.org/cmake/help/latest/command/set_tests_properties.html>`_
- :ref:`Test Configuration <test_config_args>`
