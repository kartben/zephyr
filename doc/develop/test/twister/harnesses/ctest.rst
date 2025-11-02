.. _twister_ctest_harness:

Ctest Harness
#############

Overview
********

The ``ctest`` harness enables integration of CMake's CTest framework with Twister.
CTest is CMake's built-in testing tool that provides a standardized way to run and
manage tests in CMake-based projects.

When you use the ctest harness, Twister invokes the ``ctest`` command to execute
tests that have been registered with CMake's ``add_test()`` command. This allows
you to leverage CTest's features including test selection, timeout handling, parallel
execution, and detailed result reporting.

When to Use Ctest Harness
==========================

Use the ctest harness when:

* Your test application uses CMake's ``add_test()`` to register tests
* You want to leverage CTest's advanced test management features
* You're integrating existing CTest-based tests into Twister
* You need CTest-specific features like test fixtures or resource locks
* You want to use CTest's built-in retry and timeout capabilities

The ctest harness is particularly useful for:

* Component-level unit tests registered with CMake
* Integration tests that use CTest's fixture model
* Tests that need CTest's resource management
* Existing projects migrating to Twister that already use CTest

How It Works
============

The ctest harness operates by:

1. **Building the test**: Twister builds the test application using the normal
   Zephyr build process

2. **Invoking CTest**: After building, Twister runs ``ctest`` in the build directory

3. **Passing arguments**: Any configured ``ctest_args`` are passed to the ctest command

4. **Collecting results**: Test results from CTest are captured and reported through
   Twister's reporting mechanisms

The key advantage is that you can define tests using CMake's familiar ``add_test()``
mechanism while still benefiting from Twister's platform management and reporting.

Configuration Options
*********************

The ctest harness is configured in the test's YAML file.

Basic Configuration
===================

To use the ctest harness, specify it in your test configuration:

.. code-block:: yaml

    tests:
      component.test.ctest:
        harness: ctest
        tags: unit_test

This minimal configuration will run all CTest tests registered in the build.

ctest_args: <list of arguments> (default empty)
================================================

Specifies additional arguments to pass to the ``ctest`` command. This allows you to
control CTest's behavior and select which tests to run.

.. code-block:: yaml

    tests:
      component.test.selective:
        harness: ctest
        harness_config:
          ctest_args:
            - '--repeat until-pass:5'
            - '-V'

The ``ctest_args`` option accepts any valid CTest command-line arguments.

Common CTest Arguments
======================

Here are some commonly used CTest arguments you might want to specify:

Test Selection
--------------

* ``-R <regex>``: Run tests matching regex
* ``-E <regex>``: Exclude tests matching regex
* ``-L <label>``: Run tests with specific label
* ``-LE <label>``: Exclude tests with specific label

Execution Control
-----------------

* ``-j <n>``: Run tests in parallel (use ``<n>`` jobs)
* ``--repeat <mode:n>``: Repeat tests (modes: ``until-pass``, ``until-fail``, ``after-timeout``)
* ``--timeout <seconds>``: Set test timeout
* ``--stop-on-failure``: Stop after first failure

Output Control
--------------

* ``-V``: Verbose output
* ``-VV``: Extra verbose output
* ``-Q``: Quiet output
* ``--output-on-failure``: Show output only for failed tests

Advanced Options
----------------

* ``--test-dir <dir>``: Specify test directory
* ``--schedule-random``: Run tests in random order
* ``--resource-spec-file <file>``: Specify resource spec file for resource allocation

Example Configurations
**********************

Basic CTest Configuration
==========================

Simple ctest harness with default behavior:

.. code-block:: yaml

    tests:
      unit.ctest.basic:
        harness: ctest
        tags: unit_test
        build_only: false

Retry Failed Tests
==================

Configure CTest to retry tests until they pass (useful for flaky tests):

.. code-block:: yaml

    tests:
      integration.ctest.flaky:
        harness: ctest
        harness_config:
          ctest_args:
            - '--repeat until-pass:3'
            - '--output-on-failure'
        tags: integration may_fail

Selective Test Execution
=========================

Run only tests matching a specific pattern:

.. code-block:: yaml

    tests:
      unit.ctest.selective:
        harness: ctest
        harness_config:
          ctest_args:
            - '-R ^math_'  # Run tests starting with "math_"
            - '-V'         # Verbose output
        tags: unit_test math

Verbose Debugging
=================

Extra verbose output for debugging test failures:

.. code-block:: yaml

    tests:
      debug.ctest.verbose:
        harness: ctest
        harness_config:
          ctest_args:
            - '-VV'
            - '--output-on-failure'
        tags: debug

Parallel Execution
==================

Run tests in parallel to reduce execution time:

.. code-block:: yaml

    tests:
      performance.ctest.parallel:
        harness: ctest
        harness_config:
          ctest_args:
            - '-j 4'  # Run 4 tests in parallel
        tags: performance

Multiple Arguments
==================

Combine multiple CTest features:

.. code-block:: yaml

    tests:
      comprehensive.ctest.test:
        harness: ctest
        harness_config:
          ctest_args:
            - '-R ^critical_'       # Only critical tests
            - '--repeat until-pass:3'  # Retry up to 3 times
            - '-V'                  # Verbose output
            - '--output-on-failure' # Show output on failure
            - '--timeout 30'        # 30 second timeout per test
        tags: critical regression

Registering Tests with CMake
*****************************

To use the ctest harness, you need to register tests in your ``CMakeLists.txt``
using CMake's ``add_test()`` command.

Basic Test Registration
========================

.. code-block:: cmake

    # In your test's CMakeLists.txt
    enable_testing()

    # Register a test
    add_test(
      NAME my_test
      COMMAND ${CMAKE_CURRENT_BINARY_DIR}/zephyr/zephyr.exe
    )

With Multiple Tests
===================

.. code-block:: cmake

    enable_testing()

    # Register multiple tests
    add_test(NAME test_init COMMAND test_runner init)
    add_test(NAME test_process COMMAND test_runner process)
    add_test(NAME test_cleanup COMMAND test_runner cleanup)

With Labels
===========

.. code-block:: cmake

    enable_testing()

    add_test(NAME math_add COMMAND test_runner math add)
    set_tests_properties(math_add PROPERTIES LABELS "math;quick")

    add_test(NAME math_multiply COMMAND test_runner math multiply)
    set_tests_properties(math_multiply PROPERTIES LABELS "math;quick")

    add_test(NAME io_file COMMAND test_runner io file)
    set_tests_properties(io_file PROPERTIES LABELS "io;slow")

With Dependencies
=================

.. code-block:: cmake

    enable_testing()

    add_test(NAME setup COMMAND test_runner setup)
    add_test(NAME main_test COMMAND test_runner test)
    add_test(NAME cleanup COMMAND test_runner cleanup)

    # Ensure tests run in order
    set_tests_properties(main_test PROPERTIES DEPENDS setup)
    set_tests_properties(cleanup PROPERTIES DEPENDS main_test)

Best Practices
**************

1. **Use meaningful test names**: Test names should clearly indicate what is being
   tested. These names appear in CTest and Twister reports.

2. **Apply labels**: Use CTest labels to categorize tests for selective execution:

   .. code-block:: cmake

       set_tests_properties(my_test PROPERTIES LABELS "quick;unit")

3. **Set appropriate timeouts**: Configure test-specific timeouts to prevent hanging:

   .. code-block:: cmake

       set_tests_properties(my_test PROPERTIES TIMEOUT 30)

4. **Handle test output**: Ensure tests produce clear, parseable output that CTest
   can capture and report.

5. **Group related tests**: Use CTest fixtures for tests that share setup/cleanup:

   .. code-block:: cmake

       set_tests_properties(setup PROPERTIES FIXTURES_SETUP my_fixture)
       set_tests_properties(test PROPERTIES FIXTURES_REQUIRED my_fixture)
       set_tests_properties(cleanup PROPERTIES FIXTURES_CLEANUP my_fixture)

6. **Document test requirements**: Comment your CMakeLists.txt to explain test
   dependencies and requirements.

7. **Use resource locks**: For tests that can't run in parallel:

   .. code-block:: cmake

       set_tests_properties(test PROPERTIES RESOURCE_LOCK shared_resource)

8. **Consider working directory**: Set working directory if tests need specific paths:

   .. code-block:: cmake

       set_tests_properties(test PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/data)

Running CTest Tests
********************

Running with Twister
====================

Execute ctest-based tests through Twister:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         $ ./scripts/twister -p native_sim -T tests/unit/component

   .. group-tab:: Windows

      .. code-block:: bat

         python .\scripts\twister -p native_sim -T tests\unit\component

Running CTest Directly
======================

For debugging, you can run CTest directly in the build directory:

.. code-block:: bash

    $ cd build_dir
    $ ctest -V

This can be helpful to see detailed CTest output without Twister's processing.

Debugging Failed Tests
**********************

When ctest tests fail:

1. **Check CTest output**: Look at the captured CTest output in Twister logs for
   specific failure information.

2. **Run with verbose output**: Add ``-V`` or ``-VV`` to ctest_args for more detail.

3. **Run tests individually**: Use ``-R`` to run specific failing tests in isolation.

4. **Check test registration**: Verify tests are properly registered with ``add_test()``
   in CMakeLists.txt.

5. **Verify test commands**: Ensure the command specified in ``add_test()`` is correct
   and executable.

6. **Check working directory**: Some tests may fail due to incorrect working directory.
   Set ``WORKING_DIRECTORY`` property if needed.

7. **Review test dependencies**: If using fixtures or dependencies, ensure they're
   configured correctly.

Common Issues and Solutions
****************************

**Issue**: No tests found
    **Solution**: Ensure ``enable_testing()`` is called in CMakeLists.txt and tests
    are registered with ``add_test()``. Verify build completed successfully.

**Issue**: Tests timing out
    **Solution**: Increase timeout with ``--timeout`` in ctest_args or set
    ``TIMEOUT`` property on specific tests in CMakeLists.txt.

**Issue**: Tests pass in CTest but fail in Twister
    **Solution**: Check that Twister's environment matches your local environment.
    Verify platform configuration is correct.

**Issue**: Can't run specific tests
    **Solution**: Use ``-R`` pattern matching in ctest_args. Verify test names
    match what's registered in CMakeLists.txt.

**Issue**: Parallel execution causes failures
    **Solution**: Some tests may not be safe for parallel execution. Use
    ``RESOURCE_LOCK`` property or reduce parallelism with ``-j 1``.

**Issue**: CTest not found
    **Solution**: Ensure CMake is installed and ``ctest`` is in your PATH.
    CTest comes with CMake.

Limitations
***********

Be aware of these limitations:

1. **Requires CMake knowledge**: You need to understand CMake's test registration
   to use this harness effectively.

2. **Platform-dependent**: Test commands and paths may need to be platform-specific.

3. **Limited control from YAML**: Complex CTest configuration must be done in
   CMakeLists.txt, not in testcase.yaml.

4. **Output parsing**: CTest output format may change between versions, potentially
   affecting result reporting.

5. **Single test run**: Unlike some harnesses, the ctest harness runs all registered
   tests in one invocation (unless filtered with ``-R`` or similar).

Additional Resources
********************

* `CMake CTest Documentation <https://cmake.org/cmake/help/latest/manual/ctest.1.html>`_
* `CMake add_test() Command <https://cmake.org/cmake/help/latest/command/add_test.html>`_
* `CTest Test Properties <https://cmake.org/cmake/help/latest/manual/cmake-properties.7.html#test-properties>`_
