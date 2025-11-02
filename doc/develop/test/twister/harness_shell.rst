.. _twister_harness_shell:

Shell Harness
#############

.. _twister_shell_harness:

Overview
********

The Shell harness is a specialized testing tool designed for validating shell command
functionality in Zephyr applications. It provides a streamlined way to execute shell
commands, verify their output, and ensure shell-based interfaces work correctly.

Built on top of the :ref:`pytest framework <twister_harness_pytest>`, the Shell harness
abstracts away the complexity of pytest while providing a simple, declarative YAML-based
configuration for common shell testing scenarios.

This harness is ideal for:

- **Shell command validation** - Testing individual shell commands
- **Interactive interface testing** - Verifying shell prompts and responses
- **Command sequence testing** - Validating workflows involving multiple commands
- **Output format validation** - Ensuring command outputs match expected patterns
- **Regression testing** - Detecting changes in shell command behavior

The Shell harness uses the pytest framework and twister's shell fixture under the hood,
but presents a simplified configuration interface focused specifically on shell testing.

How It Works
************

The Shell harness workflow:

1. **Device Initialization**: The test application with shell support is flashed and started
2. **Command Execution**: Each configured shell command is sent to the device
3. **Output Capture**: Responses from the shell are captured and analyzed
4. **Pattern Matching**: Output is validated against expected patterns (if specified)
5. **Result Reporting**: Success or failure is determined and reported to Twister

Unlike writing pytest tests manually, the Shell harness lets you define test scenarios
entirely in YAML, making it accessible for quick shell validation without Python knowledge.

Configuration
*************

The Shell harness supports two complementary configuration methods:

Inline Command Configuration
=============================

**shell_commands: <list of command definitions>** (default empty)
    Define shell commands and their expected outputs directly in the testcase.yaml.
    Each entry can specify:
    
    - ``command``: The shell command to execute (required)
    - ``expected``: Regular expression pattern for expected output (optional)

File-Based Configuration
========================

**shell_commands_file: <string>** (default empty)
    Path to a YAML file containing command definitions. Useful for:
    
    - Sharing test commands across multiple test scenarios
    - Managing large command sets
    - Version controlling test data separately from test configuration

If both ``shell_commands`` and ``shell_commands_file`` are provided, ``shell_commands``
takes precedence.

Basic Example
*************

Testing Kernel Commands
========================

**testcase.yaml**:

.. code-block:: yaml

    tests:
      shell.kernel.commands:
        harness: shell
        tags: shell kernel
        harness_config:
          shell_commands:
            - command: "kernel version"
              expected: "Zephyr version .*"
            - command: "kernel uptime"
              expected: "Uptime: [0-9]+ ms"
            - command: "kernel cycles"
              expected: "cycles: .* hw cycles"

This test executes three kernel-related shell commands and validates that each produces
the expected output format.

Command Without Expected Output
================================

.. code-block:: yaml

    tests:
      shell.device.info:
        harness: shell
        harness_config:
          shell_commands:
            - command: "device list"
              expected: "devices:"
            - command: "kernel sleep 100"
              # No expected output - just execute and log

Commands without ``expected`` values are executed and their output is logged, but
not validated. This is useful for commands that perform actions without meaningful
output.

Advanced Examples
*****************

Using File-Based Configuration
===============================

**testcase.yaml**:

.. code-block:: yaml

    tests:
      shell.mpu.test:
        harness: shell
        tags: shell mpu
        harness_config:
          shell_commands_file: test_mpu_commands.yml

**test_mpu_commands.yml**:

.. code-block:: yaml

    - command: "mpu mtest 1"
      expected: "The value is: 0x.*"
    - command: "mpu mtest 2"
      expected: "The value is: 0x.*"
    - command: "mpu mtest 3"
      expected: "The value is: 0x.*"

This approach keeps your testcase.yaml clean and allows reusing command sets across
different test scenarios.

Complex Output Validation
==========================

Use regular expressions for flexible pattern matching:

.. code-block:: yaml

    tests:
      shell.gpio.test:
        harness: shell
        harness_config:
          shell_commands:
            - command: "gpio get GPIO_0 0"
              expected: "Value [01]"
            - command: "gpio conf GPIO_0 0 out"
              expected: "(configured|success)"
            - command: "gpio set GPIO_0 0 1"
              expected: ".*"  # Matches any output

Multi-Step Test Workflows
==========================

Test command sequences that build on each other:

.. code-block:: yaml

    tests:
      shell.i2c.workflow:
        harness: shell
        tags: shell i2c
        depends_on: i2c
        harness_config:
          shell_commands:
            # Scan for I2C devices
            - command: "i2c scan I2C_0"
              expected: "devices found on I2C_0"
            
            # Read from detected device
            - command: "i2c read I2C_0 0x48 0x00 1"
              expected: "([0-9a-fA-F]{2})"
            
            # Verify configuration
            - command: "i2c read I2C_0 0x48 0x01 1"
              expected: "[0-9a-fA-F]{2}"

Testing Shell Features
======================

Validate shell infrastructure itself:

.. code-block:: yaml

    tests:
      shell.infrastructure:
        harness: shell
        harness_config:
          shell_commands:
            # Test help system
            - command: "help"
              expected: "Available commands:"
            
            # Test command completion/listing
            - command: "kernel"
              expected: "(version|uptime|reboot|threads)"
            
            # Test error handling
            - command: "invalid_command_xyz"
              expected: "(unknown|not found|error)"

Default Command File
********************

If no ``shell_commands`` or ``shell_commands_file`` is specified, the Shell harness
looks for a default file named ``test_shell.yml`` in the test directory.

This allows a simple configuration:

.. code-block:: yaml

    tests:
      shell.default:
        harness: shell  # Will use test_shell.yml if it exists

Regular Expression Patterns
****************************

The ``expected`` field uses Python regular expressions. Common patterns:

- ``.*`` - Match any characters (including none)
- ``[0-9]+`` - One or more digits
- ``[0-9a-fA-F]{2}`` - Exactly two hexadecimal digits
- ``(option1|option2)`` - Match either option1 or option2
- ``\d+`` - One or more digits (same as ``[0-9]+``)
- ``\s+`` - One or more whitespace characters
- ``.*success.*`` - Contains "success" anywhere in the output

Escape special regex characters:

- Use ``\.`` for literal dot
- Use ``\(`` and ``\)`` for literal parentheses
- Use ``\[`` and ``\]`` for literal brackets

Best Practices
**************

1. **Be Specific with Patterns**: Make patterns specific enough to catch errors but
   flexible enough to handle minor variations:

   .. code-block:: yaml

       # Too specific - may break with version changes
       expected: "Zephyr version 3.5.0"
       
       # Better - validates format but flexible
       expected: "Zephyr version [0-9]+\.[0-9]+\.[0-9]+"

2. **Test Command Errors**: Include tests for invalid commands to verify error handling:

   .. code-block:: yaml

       - command: "invalid_cmd"
         expected: "command not found"

3. **Order Commands Logically**: Arrange commands in a meaningful sequence that
   reflects actual usage

4. **Use Descriptive Test Names**: Name your test scenarios to clearly indicate what
   shell functionality is being tested

5. **Document Expected Patterns**: Add comments explaining complex regex patterns:

   .. code-block:: yaml

       - command: "sensor read"
         # Expects: "Temperature: 25.3 C, Humidity: 60.5 %"
         expected: "Temperature: [0-9.]+.*Humidity: [0-9.]+.*"

6. **Keep Commands Independent**: Each command should work regardless of previous
   command results when possible

7. **Verify Critical Output**: Focus patterns on the most important parts of the output

Integration with Pytest
************************

The Shell harness is implemented using pytest. If you need more sophisticated testing
beyond what the declarative YAML configuration provides, consider using the
:ref:`Pytest harness <twister_harness_pytest>` directly with the ``shell`` fixture.

This gives you access to:

- Conditional logic in tests
- Variable command parameters
- Dynamic output parsing
- Complex assertions
- Test parametrization

Example using pytest directly:

.. code-block:: python

    def test_gpio_sequence(shell):
        """More complex test with logic"""
        # Configure GPIO
        shell.write('gpio conf GPIO_0 5 out')
        assert 'configured' in shell.readline().lower()
        
        # Test toggling
        for state in [1, 0, 1, 0]:
            shell.write(f'gpio set GPIO_0 5 {state}')
            shell.write('gpio get GPIO_0 5')
            output = shell.readline()
            assert f'Value {state}' in output

Troubleshooting
***************

Common Issues
=============

**Command Not Recognized**
    - Verify the shell command is actually available in your application
    - Check that required subsystems are enabled (CONFIG_SHELL=y)
    - Ensure the command module is compiled in

**Pattern Doesn't Match**
    - Run with ``twister -v`` to see actual output
    - Check for extra whitespace or newlines
    - Use ``.*`` to allow flexibility: ``".*expected text.*"``
    - Remember regex special characters need escaping

**Test Times Out**
    - Increase test timeout in testcase.yaml
    - Verify device is actually executing commands
    - Check serial baud rate for hardware testing
    - Ensure the shell prompt is appearing

**Shell Prompt Issues**
    - The shell fixture handles prompts automatically
    - If using custom prompts, they should still work
    - Check console output for any initialization errors

**File Not Found (shell_commands_file)**
    - Path is relative to the test directory
    - Check file exists at specified path
    - Verify YAML syntax in the command file

Examples in Zephyr Tree
***********************

See real-world examples in the Zephyr repository:

- ``samples/subsys/shell/`` - Shell sample applications
- ``tests/subsys/shell/`` - Shell subsystem tests

Example Test Application
************************

**testcase.yaml**:

.. code-block:: yaml

    common:
      tags: shell
    tests:
      shell.test.basic:
        harness: shell
        min_ram: 32
        harness_config:
          shell_commands:
            - command: "help"
              expected: "Available commands"
            - command: "demo ping"
              expected: "pong"
            - command: "demo stats"
              expected: "counter: [0-9]+"

**prj.conf**:

.. code-block:: cfg

    CONFIG_SHELL=y
    CONFIG_SHELL_BACKEND_SERIAL=y

**src/main.c**:

.. code-block:: c

    #include <zephyr/kernel.h>
    #include <zephyr/shell/shell.h>

    static int cmd_ping(const struct shell *sh, size_t argc, char **argv)
    {
        shell_print(sh, "pong");
        return 0;
    }

    static int counter = 0;

    static int cmd_stats(const struct shell *sh, size_t argc, char **argv)
    {
        shell_print(sh, "counter: %d", counter++);
        return 0;
    }

    SHELL_STATIC_SUBCMD_SET_CREATE(sub_demo,
        SHELL_CMD(ping, NULL, "Ping command", cmd_ping),
        SHELL_CMD(stats, NULL, "Show statistics", cmd_stats),
        SHELL_SUBCMD_SET_END
    );

    SHELL_CMD_REGISTER(demo, &sub_demo, "Demo commands", NULL);

    void main(void)
    {
        /* Application code */
    }

See Also
********

- :ref:`Pytest Harness <twister_harness_pytest>` for more advanced Python-based testing
- :ref:`Shell Subsystem <shell_api>` documentation
- :ref:`Console Harness <twister_harness_console>` for custom output validation
- `Python Regular Expressions <https://docs.python.org/3/library/re.html>`_
