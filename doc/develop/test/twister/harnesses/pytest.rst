.. _twister_pytest_harness:

Pytest Harness
##############

Overview
********

The ``pytest`` harness enables integration of Python-based pytest test suites with Twister.
This harness is particularly powerful for testing scenarios that require complex test logic,
external tool integration, or sophisticated validation that goes beyond simple pattern matching.

The pytest harness leverages the full capabilities of the pytest framework, including fixtures,
parametrization, markers, and plugins. It allows you to write comprehensive test scenarios in
Python while still benefiting from Twister's platform management, build automation, and reporting
capabilities.

For more information about using pytest with Zephyr, see :ref:`integration_with_pytest`.

When to Use Pytest Harness
===========================

Use the pytest harness when:

* You need complex test logic that goes beyond simple output pattern matching
* Your test requires interaction with external tools or services
* You want to use Python's rich ecosystem of testing and validation libraries
* You need to perform calculations or data processing on test results
* You want to share test fixtures and utilities across multiple tests
* Your test involves multiple phases or conditional execution paths

The pytest harness is especially useful for:

* Shell command testing and validation
* Network protocol testing
* File system and storage testing
* Performance measurement and analysis
* Integration testing with external systems

Configuration Options
*********************

The pytest harness is configured in the test's YAML file under ``harness_config``.
The following options control pytest execution:

.. _pytest_root:

pytest_root: <list of pytest testpaths> (default pytest)
    Specifies one or more pytest directories, files, or subtests to execute when the test
    scenario runs. The default pytest directory is ``pytest`` relative to the test directory.

    After the pytest run completes, Twister checks the pytest report to determine if the
    test scenario passed or failed.

    The ``pytest_root`` option accepts various path formats:

    * **Relative paths**: Relative to the test directory
    * **Absolute paths**: Full filesystem paths
    * **Environment variable paths**: Using ``$VARIABLE`` syntax
    * **Home directory paths**: Using ``~`` prefix
    * **Specific test selection**: Using ``::`` to select individual tests or parametrized tests

    Example configurations:

    .. code-block:: yaml

        harness_config:
          pytest_root:
            - "pytest/test_shell_help.py"                    # Relative path to file
            - "../shell/pytest/test_shell.py"                # Parent directory reference
            - "/tmp/test_shell.py"                           # Absolute path
            - "~/tmp/test_shell.py"                          # Home directory path
            - "$ZEPHYR_BASE/samples/subsys/testsuite/pytest/shell/pytest/test_shell.py"  # Environment variable
            - "pytest/test_shell_help.py::test_shell2_sample"              # Select specific test
            - "pytest/test_shell_help.py::test_shell2_sample[param_a]"     # Select parametrized test

    .. tip::
       You can specify multiple pytest roots to run different test files or even combine
       tests from different directories in a single test scenario.

.. _pytest_args:

pytest_args: <list of arguments> (default empty)
    Additional command-line arguments to pass to ``pytest``. These arguments are appended
    to the pytest command when Twister launches the test.

    Common use cases include:

    * Filtering tests with ``-k``
    * Adjusting verbosity with ``-v`` or ``-q``
    * Setting log levels with ``--log-level``
    * Enabling specific pytest plugins
    * Controlling output format
    * Setting timeouts or retries

    Example:

    .. code-block:: yaml

        harness_config:
          pytest_args:
            - '-k=test_method'           # Only run tests matching 'test_method'
            - '--log-level=DEBUG'        # Set logging level to DEBUG
            - '-v'                       # Verbose output
            - '--tb=short'              # Shorter traceback format

    .. note::
       When running Twister, you can also use ``--pytest-args`` on the command line to
       pass arguments to pytest. Command-line arguments can be specified multiple times
       and are combined with the configuration file arguments.

.. _pytest_dut_scope:

pytest_dut_scope: <function|class|module|package|session> (default function)
    Controls the lifecycle and sharing of the Device Under Test (DUT) for pytest fixtures.
    This determines when the DUT is launched and how it's shared across tests.

    Available scopes:

    * ``function``: DUT is launched separately for each test function (default)
    * ``class``: DUT is shared across all test methods in a test class
    * ``module``: DUT is shared across all tests in a Python module
    * ``package``: DUT is shared across all tests in a Python package
    * ``session``: DUT is launched once for the entire pytest session

    The scope affects both the ``dut`` and ``shell`` pytest fixtures provided by Twister.

    .. code-block:: yaml

        harness_config:
          pytest_dut_scope: session    # Launch DUT once for all tests

    .. tip::
       Use ``session`` scope for faster test execution when tests don't require a clean
       DUT state. Use ``function`` scope when tests need isolation and a fresh DUT state.

    .. warning::
       When using broader scopes (``module``, ``session``), ensure your tests don't leave
       the DUT in a state that could affect subsequent tests. Consider cleanup in
       teardown fixtures.

Example Configurations
**********************

Basic Pytest Harness
====================

Simple pytest harness configuration using default settings:

.. code-block:: yaml

    common:
      harness: pytest
    tests:
      sample.pytest.simple:
        # Uses default pytest_root: "pytest"
        # All pytest files in the pytest/ directory will be discovered and run

Specifying Multiple Test Directories
=====================================

Running tests from multiple directories:

.. code-block:: yaml

    common:
      harness: pytest
    tests:
      pytest.example.directories:
        harness_config:
          pytest_root:
            - pytest_dir1
            - $ENV_VAR/samples/test/pytest_dir2

Selecting Specific Tests and Subtests
======================================

Running specific test files and individual test cases:

.. code-block:: yaml

    common:
      harness: pytest
    tests:
      pytest.example.files_and_subtests:
        harness_config:
          pytest_root:
            - pytest/test_file_1.py              # Run all tests in this file
            - test_file_2.py::test_A             # Run only test_A
            - test_file_2.py::test_B[param_a]    # Run only parametrized test with param_a

Session-Scoped DUT with Custom Arguments
=========================================

Advanced configuration with session-scoped DUT and custom pytest arguments:

.. code-block:: yaml

    tests:
      sample.pytest.advanced:
        harness: pytest
        harness_config:
          pytest_dut_scope: session
          pytest_root:
            - pytest/test_performance.py
          pytest_args:
            - '--log-level=INFO'
            - '-v'
            - '--maxfail=1'

Integration with Required Applications
=======================================

Using pytest with required applications (see ``required_applications`` in test configuration):

.. code-block:: yaml

    tests:
      sample.pytest.with_deps:
        harness: pytest
        harness_config:
          pytest_root:
            - pytest/test_integration.py
        required_applications:
          - name: sample.shared_app
          - name: sample.basic.helloworld
            platform: native_sim

The pytest test can access build directories of required applications through the
``required_build_dirs`` fixture.

Writing Pytest Tests for Twister
*********************************

Test Structure
==============

Pytest tests for Twister follow standard pytest conventions. Tests should be placed in
the directory structure specified by ``pytest_root`` (default: ``pytest/``).

A typical test file structure:

.. code-block:: python

    import pytest

    def test_basic_functionality(dut):
        """Test basic device functionality."""
        dut.readlines_until(regex=".*Initialization complete.*")
        # Test logic here

    def test_with_shell(shell):
        """Test using shell interface."""
        output = shell.exec_command("version")
        assert "Zephyr" in output

    @pytest.mark.parametrize("input,expected", [
        ("hello", "HELLO"),
        ("world", "WORLD"),
    ])
    def test_parametrized(shell, input, expected):
        """Parametrized test example."""
        output = shell.exec_command(f"echo {input}")
        assert expected in output.upper()

Available Fixtures
==================

Twister provides several pytest fixtures for interacting with the DUT:

``dut``
    The Device Under Test fixture. Provides access to the device's console output
    and basic interaction methods.

``shell``
    A shell interface fixture for sending commands and receiving responses.
    Built on top of the ``dut`` fixture with convenience methods for shell interaction.

``required_build_dirs``
    Dictionary mapping application names to their build directories when using
    ``required_applications``.

Best Practices
**************

1. **Organize tests logically**: Group related tests in the same file or directory.
   Use pytest classes to group related test methods.

2. **Use fixtures for setup/teardown**: Leverage pytest fixtures for test setup
   and cleanup rather than doing it in test functions.

3. **Write isolated tests**: Each test should be independent and not rely on state
   from previous tests, especially when using ``function`` scope.

4. **Use parametrization**: When testing multiple inputs or conditions, use
   ``@pytest.mark.parametrize`` to reduce code duplication.

5. **Add descriptive docstrings**: Document what each test validates and why.
   This helps with understanding test failures.

6. **Use appropriate assertions**: Pytest provides rich assertion introspection.
   Use simple ``assert`` statements with clear comparison expressions.

7. **Consider scope carefully**: Choose ``pytest_dut_scope`` based on test requirements.
   Use ``function`` for isolation, broader scopes for performance.

8. **Handle timeouts gracefully**: Tests running on hardware may need longer timeouts.
   Use pytest timeout plugins or custom timeout handling as needed.

9. **Test one thing at a time**: Keep tests focused on a single aspect of functionality.
   Multiple small tests are better than one large test.

10. **Use markers for test organization**: Apply pytest marks to categorize tests
    (e.g., ``@pytest.mark.slow``, ``@pytest.mark.hardware``).

Running Tests Locally
**********************

To run pytest tests through Twister:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         $ ./scripts/twister -p native_sim -T samples/subsys/testsuite/pytest

   .. group-tab:: Windows

      .. code-block:: bat

         python .\scripts\twister -p native_sim -T samples\subsys\testsuite\pytest

To run pytest tests directly (for debugging):

.. code-block:: bash

    $ cd path/to/test
    $ pytest pytest/

Debugging Failed Tests
***********************

When a pytest test fails:

1. **Check the test output**: Twister captures pytest output in the test logs.
   Look for assertion failures and error messages.

2. **Run pytest directly**: Execute pytest outside of Twister for more interactive
   debugging with ``--pdb`` flag.

3. **Increase verbosity**: Use ``pytest_args: ['-v', '-s']`` to see more output.

4. **Check fixture scope**: Ensure fixture scopes are appropriate for your test's
   needs.

5. **Verify DUT state**: Confirm the device is in the expected state before running
   tests, especially with broader scopes.

Common Issues and Solutions
****************************

**Issue**: Tests fail intermittently
    **Solution**: Check for timing issues, race conditions, or insufficient timeouts.
    Ensure tests don't depend on previous test state.

**Issue**: DUT not responding
    **Solution**: Verify the device is properly connected and flashed. Check that
    the correct platform is specified. Ensure the DUT fixture scope is appropriate.

**Issue**: Import errors in pytest
    **Solution**: Ensure all required Python packages are installed. Check Python
    path configuration and pytest plugin installation.

**Issue**: Tests pass locally but fail in CI
    **Solution**: Check for platform-specific issues, timing differences, or
    environmental dependencies. Ensure all test dependencies are documented.
