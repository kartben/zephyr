.. _twister_pytest_harness:

Pytest Harness
##############

The ``pytest`` harness integrates Python's pytest testing framework with Twister, enabling
sophisticated test scenarios that require host-side logic, external tool interaction, or
complex validation beyond what device-only testing can provide. This harness is ideal for
tests that need to interact with the device under test (DUT) while executing Python code
on the host machine.

Overview
********

The pytest harness allows you to write test logic in Python using pytest, while Twister
handles the building, flashing, and device management. This combination enables:

- Host-side test orchestration and validation
- Interaction with the device through serial communication
- Integration with external tools and services
- Complex test scenarios involving multiple devices or components
- Advanced assertions and data validation in Python

The pytest harness launches the DUT, provides fixtures for device interaction, and evaluates
the pytest test results to determine the overall test outcome.

How It Works
************

When you use the pytest harness:

1. Twister builds the test application
2. Twister flashes the application to the target device (or starts an emulator)
3. Twister launches pytest with the specified test files
4. Pytest executes the test cases, which can interact with the DUT via provided fixtures
5. Twister collects the pytest results and reports pass/fail status

For more details on the integration architecture, see :ref:`integration_with_pytest`.

Configuration Options
*********************

The pytest harness is configured in the ``harness_config`` section of your test's
``testcase.yaml`` file.

.. _pytest_root:

pytest_root: <list of pytest testpaths> (default pytest)
    Specifies pytest directories, files, or specific test functions to execute.
    The default is a directory named ``pytest`` relative to the test application.
    
    You can specify:
    
    - Directory paths (absolute or relative)
    - Specific test files
    - Individual test functions using pytest's ``::`` notation
    - Parametrized test variants
    
    Example:

    .. code-block:: yaml

        harness_config:
          pytest_root:
            - "pytest/test_shell_help.py"
            - "../shell/pytest/test_shell.py"
            - "/tmp/test_shell.py"
            - "~/tmp/test_shell.py"
            - "$ZEPHYR_BASE/samples/subsys/testsuite/pytest/shell/pytest/test_shell.py"
            - "pytest/test_shell_help.py::test_shell2_sample"
            - "pytest/test_shell_help.py::test_shell2_sample[param_a]"

.. _pytest_args:

pytest_args: <list of arguments> (default empty)
    Additional command-line arguments to pass to pytest. These are passed directly to
    pytest when it's invoked.
    
    Example:

    .. code-block:: yaml

        harness_config:
          pytest_args:
            - '-k=test_method'
            - '--log-level=DEBUG'
            - '-v'

    Note: The ``--pytest-args`` option can also be passed on the Twister command line
    to provide arguments for all pytest tests in a run.

.. _pytest_dut_scope:

pytest_dut_scope: <function|class|module|package|session> (default function)
    Controls the lifecycle scope of the ``dut`` (Device Under Test) fixture provided
    to pytest tests. This determines when the device is initialized and torn down:
    
    - ``function``: Device is reset for every test function (default)
    - ``class``: Device is shared across all tests in a class
    - ``module``: Device is shared across all tests in a module
    - ``package``: Device is shared across all tests in a package
    - ``session``: Device is initialized once for the entire pytest session
    
    Use ``session`` scope for faster test execution when tests don't require device
    resets, or ``function`` scope for complete isolation between tests.

Basic Example
*************

Here's a simple example of a test using the pytest harness:

.. code-block:: yaml

   common:
     harness: pytest
   tests:
     pytest.example:
       harness_config:
         pytest_root:
           - pytest_dir1
           - $ZEPHYR_BASE/samples/test/pytest_dir2

Multiple Test Files Example
****************************

You can specify multiple test files or even specific test functions:

.. code-block:: yaml

   tests:
     pytest.example.files_and_subtests:
       harness: pytest
       harness_config:
         pytest_root:
           - pytest/test_file_1.py
           - test_file_2.py::test_A
           - test_file_2.py::test_B[param_a]

Advanced Configuration Example
*******************************

This example shows using multiple configuration options:

.. code-block:: yaml

   tests:
     pytest.advanced_example:
       harness: pytest
       harness_config:
         pytest_root:
           - pytest/test_advanced.py
         pytest_args:
           - '--log-level=DEBUG'
           - '-v'
           - '--tb=short'
         pytest_dut_scope: session
         fixture: fixture_serial

Writing Pytest Tests
*********************

When writing pytest tests for use with Twister, you have access to special fixtures:

The ``dut`` Fixture
===================

The ``dut`` (Device Under Test) fixture provides access to the running device:

.. code-block:: python

   def test_device_output(dut):
       # Wait for specific output
       dut.expect("System ready")
       
       # Send data to device
       dut.write("test command")
       
       # Verify response
       dut.expect("Command executed")

The ``shell`` Fixture
=====================

For devices with shell support, the ``shell`` fixture provides high-level shell interaction:

.. code-block:: python

   def test_shell_commands(shell):
       # Execute shell command
       output = shell.exec_command("kernel version")
       
       # Validate output
       assert "Zephyr" in output

Directory Structure
*******************

A typical test directory with pytest harness looks like:

.. code-block:: none

   my_test/
   ├── CMakeLists.txt
   ├── prj.conf
   ├── src/
   │   └── main.c
   ├── testcase.yaml
   └── pytest/
       ├── __init__.py
       ├── test_basic.py
       └── test_advanced.py

The ``pytest`` directory (or whatever you specify in ``pytest_root``) contains your
Python test files.

Integration with Required Applications
***************************************

The pytest harness can work with the ``required_applications`` feature to access build
artifacts from other test scenarios. Build directories are passed via ``--required-build``
arguments and are accessible through the ``required_build_dirs`` fixture.

Example:

.. code-block:: yaml

   tests:
     sample.required_app_demo:
       harness: pytest
       required_applications:
         - name: sample.shared_app
         - name: sample.basic.helloworld
           platform: native_sim

Use Cases
*********

The pytest harness excels in scenarios requiring:

**Host-Side Validation**
    Complex assertions, data analysis, or validation logic that's easier to implement
    in Python than in C.

**External Tool Integration**
    Tests that need to interact with external tools, databases, or services during
    test execution.

**Multi-Device Testing**
    Scenarios involving multiple devices or components that need coordination from
    the host.

**Data Processing**
    Tests that collect and analyze large amounts of data from the device, perform
    statistical analysis, or generate reports.

**Interactive Testing**
    Tests that need to send commands and validate responses in a interactive manner.

Best Practices
**************

1. **Organize test files**: Use a clear directory structure and naming convention for
   pytest test files.

2. **Use appropriate scope**: Choose ``pytest_dut_scope`` based on your test requirements.
   Use ``function`` for isolation, ``session`` for speed.

3. **Leverage fixtures**: Use pytest fixtures effectively to share setup code and
   test resources.

4. **Handle timeouts**: Be mindful of device response times and set appropriate timeouts
   in your pytest code.

5. **Clean up resources**: Ensure your tests properly clean up any resources they use,
   especially when using broader fixture scopes.

6. **Write portable tests**: Avoid hard-coding platform-specific details when possible.

7. **Document dependencies**: Clearly document any external tools or Python packages
   your tests require.

See Also
********

- :ref:`integration_with_pytest` - Detailed pytest integration documentation
- :ref:`pytest` - Main pytest framework documentation
- :ref:`twister_harnesses` - Overview of all available harnesses
- :ref:`twister_script` - Main Twister documentation
