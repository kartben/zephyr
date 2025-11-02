.. _twister_harness_pytest:

Pytest Harness
##############

Overview
********

The Pytest harness integrates Python's popular pytest testing framework with Twister,
enabling sophisticated integration testing, hardware interaction testing, and test
scenarios that benefit from Python's rich ecosystem of libraries and tools.

This harness is ideal for:

- **Integration testing** where tests need to interact with the DUT (Device Under Test)
- **Shell command testing** that requires parsing and validating command outputs
- **Multi-device scenarios** requiring orchestration between devices
- **Complex test logic** that's easier to express in Python than C
- **Tests requiring external tools** or services that have Python APIs

The Pytest harness runs pytest test suites alongside your Zephyr application, providing
fixtures that allow your Python test code to communicate with the running device through
its serial console or other interfaces.

How It Works
************

The Pytest harness workflow:

1. **Application Build**: Twister builds your Zephyr test application
2. **Device Launch**: The application is flashed and started on the target or simulator
3. **Pytest Execution**: Twister launches pytest with your specified test files
4. **DUT Communication**: pytest fixtures provide access to the running device
5. **Result Collection**: Test results are collected and reported back to Twister

Key pytest fixtures provided by the harness:

- ``dut``: Provides access to the Device Under Test for sending commands and reading output
- ``shell``: Specialized fixture for shell-based testing
- ``required_build_dirs``: Access to build artifacts from required applications

Configuration
*************

The Pytest harness supports several configuration options to control test execution:

.. _pytest_root:

**pytest_root: <list of pytest testpaths>** (default ``pytest``)
    Specifies which pytest files, directories, or specific tests to execute.
    You can provide:
    
    - Directory paths (relative or absolute)
    - Specific test files
    - Individual test functions using pytest's ``::`` syntax
    - Parametrized test variants
    - Environment variable substitutions (``$VARIABLE``)

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

**pytest_args: <list of arguments>** (default empty)
    Additional command-line arguments to pass to pytest. Common options include:
    
    - ``-k=pattern``: Run only tests matching the pattern
    - ``--log-level=DEBUG``: Set logging level
    - ``-v``: Verbose output
    - ``-s``: Don't capture output (useful for debugging)
    - ``--tb=short``: Shorter traceback format

    Example:

    .. code-block:: yaml

        harness_config:
          pytest_args:
            - '-k=test_method'
            - '--log-level=DEBUG'
            - '-v'

.. _pytest_dut_scope:

**pytest_dut_scope: <function|class|module|package|session>** (default ``function``)
    Controls the lifecycle of the DUT fixture (when the device is started and stopped):
    
    - ``function``: Device is restarted for each test function (most isolated)
    - ``class``: Device is shared across tests in a test class
    - ``module``: Device is shared across all tests in a Python module
    - ``package``: Device is shared across tests in a package
    - ``session``: Device is started once for all tests (fastest, least isolated)

    Example:

    .. code-block:: yaml

        harness_config:
          pytest_dut_scope: session

Basic Example
*************

**testcase.yaml**:

.. code-block:: yaml

    common:
      harness: pytest
    tests:
      sample.shell.pytest:
        tags: shell
        depends_on: gpio

**pytest/test_shell.py**:

.. code-block:: python

    import pytest

    def test_shell_help(dut):
        """Test that help command works"""
        dut.write('help')
        output = dut.readline()
        assert 'Available commands' in output

    def test_shell_version(dut):
        """Test version command"""
        dut.write('kernel version')
        output = dut.readline()
        assert 'Zephyr version' in output

Advanced Examples
*****************

Testing Multiple Commands
==========================

.. code-block:: python

    def test_command_sequence(dut):
        """Test a sequence of shell commands"""
        commands = [
            ('device list', 'devices:'),
            ('kernel uptime', 'Uptime:'),
            ('kernel threads', 'Threads:'),
        ]
        
        for cmd, expected in commands:
            dut.write(cmd)
            output = dut.readline()
            assert expected in output, f"Expected '{expected}' in output for '{cmd}'"

Parametrized Tests
==================

Use pytest's parametrization for testing with different inputs:

.. code-block:: python

    import pytest

    @pytest.mark.parametrize("sleep_ms,tolerance", [
        (100, 10),
        (500, 20),
        (1000, 30),
    ])
    def test_kernel_sleep(dut, sleep_ms, tolerance):
        """Test kernel sleep with various durations"""
        import time
        
        start = time.time()
        dut.write(f'kernel sleep {sleep_ms}')
        dut.readline()  # Wait for command to complete
        elapsed_ms = (time.time() - start) * 1000
        
        assert abs(elapsed_ms - sleep_ms) < tolerance

Using Test Classes
==================

Organize related tests using pytest classes:

.. code-block:: python

    class TestGPIOCommands:
        """Test GPIO-related shell commands"""
        
        def test_gpio_get(self, dut):
            dut.write('gpio get GPIO_0 0')
            output = dut.readline()
            assert 'Value' in output
        
        def test_gpio_conf(self, dut):
            dut.write('gpio conf GPIO_0 0 out')
            output = dut.readline()
            assert 'configured' in output.lower()

Accessing Build Artifacts
==========================

When using ``required_applications``, access their build directories:

.. code-block:: python

    def test_with_required_app(dut, required_build_dirs):
        """Test using artifacts from required applications"""
        # required_build_dirs is a dict mapping app names to build paths
        shared_app_build = required_build_dirs['sample.shared_app']
        
        # Access files from the required app's build
        config_file = shared_app_build / 'zephyr' / '.config'
        assert config_file.exists()

Configuration with Multiple Test Files
***************************************

**testcase.yaml**:

.. code-block:: yaml

    common:
      harness: pytest
    tests:
      pytest.example.directories:
        harness_config:
          pytest_root:
            - pytest_dir1
            - $ENV_VAR/samples/test/pytest_dir2
      
      pytest.example.files_and_subtests:
        harness_config:
          pytest_root:
            - pytest/test_file_1.py
            - test_file_2.py::test_A
            - test_file_2.py::test_B[param_a]

Best Practices
**************

1. **Use Descriptive Test Names**: Follow pytest conventions with clear, descriptive
   function names starting with ``test_``.

2. **Leverage Fixtures**: Use pytest fixtures for common setup/teardown logic. The
   ``dut`` fixture is automatically available.

3. **Keep Tests Independent**: Each test should be able to run independently. Don't
   rely on state from previous tests.

4. **Use Appropriate Scope**: Choose ``pytest_dut_scope`` based on your needs:
   
   - Use ``function`` for maximum isolation (default)
   - Use ``session`` for faster execution when tests don't interfere

5. **Add Timeouts**: For commands that might hang, consider implementing timeouts:

   .. code-block:: python

       import pytest
       
       @pytest.mark.timeout(30)
       def test_long_operation(dut):
           dut.write('long_command')
           # Test has 30 seconds to complete

6. **Clear Output Buffer**: Before reading expected output, ensure the buffer is clear:

   .. code-block:: python

       dut.clear_buffer()
       dut.write('command')
       output = dut.readline()

7. **Use Assertions Effectively**: Provide clear assertion messages:

   .. code-block:: python

       assert value in output, f"Expected '{value}' but got '{output}'"

Working with Shell Fixture
**************************

The ``shell`` fixture is a specialized version of ``dut`` optimized for shell testing:

.. code-block:: python

    def test_shell_autocomplete(shell):
        """Test shell autocomplete feature"""
        shell.write('ker\t')  # Send tab for autocomplete
        output = shell.readline()
        assert 'kernel' in output

Integration with Other Tools
****************************

The Pytest harness can integrate with Python's rich ecosystem:

.. code-block:: python

    import json
    import re
    from pathlib import Path

    def test_json_output(dut):
        """Test parsing JSON from device output"""
        dut.write('get_status --format json')
        output = dut.readline()
        
        # Parse JSON output
        status = json.loads(output)
        assert status['state'] == 'ready'
        assert status['uptime'] > 0

    def test_with_external_validation(dut):
        """Test using external validation"""
        dut.write('sensor read')
        output = dut.readline()
        
        # Extract temperature value
        match = re.search(r'Temperature: ([\d.]+)', output)
        assert match
        
        temp = float(match.group(1))
        assert 20.0 <= temp <= 30.0, "Temperature out of expected range"

Debugging Pytest Tests
**********************

When debugging pytest tests:

1. **Use Print Statements**: pytest will show print output when tests fail

   .. code-block:: python

       def test_debug(dut):
           dut.write('command')
           output = dut.readline()
           print(f"DEBUG: Got output: {output}")
           assert expected in output

2. **Run with Verbose Flags**:

   .. code-block:: yaml

       harness_config:
         pytest_args: ['-v', '-s']  # -s shows print statements

3. **Use pytest's Built-in Debugger**:

   .. code-block:: yaml

       harness_config:
         pytest_args: ['--pdb']  # Drop into debugger on failure

4. **Capture More Logging**:

   .. code-block:: yaml

       harness_config:
         pytest_args: ['--log-level=DEBUG', '--log-cli-level=DEBUG']

Troubleshooting
***************

Common Issues
=============

**Test Can't Find DUT**
    - Verify the device is properly connected and recognized
    - Check that ``--device-testing`` is used with proper device configuration
    - Ensure the DUT boots and produces console output

**Timeout Errors**
    - Increase the test timeout in testcase.yaml
    - Check if the command is actually completing
    - Verify baud rate settings for hardware testing

**Pytest Not Found**
    - Ensure pytest is installed: ``pip install pytest``
    - Check that pytest is in your PATH
    - Verify the twister pytest integration is properly installed

**Import Errors in Tests**
    - Ensure test files are in a valid Python package (with ``__init__.py``)
    - Check your PYTHONPATH if using custom modules
    - Verify all required Python packages are installed

**DUT Scope Issues**
    - If tests interfere with each other, use ``pytest_dut_scope: function``
    - If device startup is slow, use ``pytest_dut_scope: session`` for speed
    - Remember: broader scope = faster, but less isolation

See Also
********

- `Pytest Documentation <https://docs.pytest.org/>`_
- :ref:`Integration with Pytest <integration_with_pytest>`
- :ref:`Shell Harness <twister_harness_shell>` for simplified shell testing
- :ref:`Test Configuration <test_config_args>`
- `Pytest Fixtures <https://docs.pytest.org/en/stable/fixture.html>`_
- `Pytest Parametrization <https://docs.pytest.org/en/stable/parametrize.html>`_
