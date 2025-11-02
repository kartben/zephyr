.. _twister_shell_harness:

Shell Harness
#############

The ``shell`` harness enables automated testing of Zephyr's shell subsystem by executing
shell commands on the device and validating their output. This harness leverages the
pytest framework to provide interactive command-response testing with flexible output
validation.

Overview
********

Zephyr's shell subsystem provides a command-line interface for interacting with devices
during development and debugging. The shell harness automates testing of:

- Shell command functionality
- Command output format and content
- Shell subsystem behavior
- Command-line utilities and tools

By combining pytest's testing capabilities with shell interaction, this harness enables
comprehensive validation of shell-based functionality without manual intervention.

How It Works
************

The shell harness workflow:

1. Twister builds and flashes the test application with shell enabled
2. The harness connects to the device's shell interface (typically via UART)
3. Shell commands are executed sequentially or as defined in test configuration
4. Command output is captured and validated against expected patterns
5. Results are reported through pytest's test framework

This automation ensures shell commands work correctly and produce expected output
across different platforms and configurations.

Configuration Options
*********************

The shell harness supports two ways to specify shell commands to execute:

shell_commands: <list of command/expected pairs> (default empty)
    Directly specify shell commands and their expected output in the test configuration.
    Each entry contains:
    
    - ``command``: Shell command to execute
    - ``expected``: Optional regex pattern to match in output
    
    If ``expected`` is omitted, the command is executed and output is logged but not
    validated.

shell_commands_file: <string> (default empty)
    Path to a YAML file containing shell commands and expected output. The file
    format is the same as ``shell_commands`` entries.
    
    If not specified, the harness looks for ``test_shell.yml`` in the test directory.
    
    Note: ``shell_commands`` takes precedence over ``shell_commands_file`` if both
    are specified.

Basic Example
*************

Inline command specification:

.. code-block:: yaml

   tests:
     shell.basic:
       harness: shell
       harness_config:
         shell_commands:
           - command: "kernel cycles"
             expected: "cycles: .* hw cycles"
           - command: "kernel version"
             expected: "Zephyr version .*"
           - command: "kernel sleep 100"

In this example:

- First two commands validate output with regex patterns
- Third command just executes without validation (no ``expected`` field)

Using Command File
******************

For better organization, define commands in a separate file:

.. code-block:: yaml
   :caption: test_shell.yml

   - command: "mpu mtest 1"
     expected: "The value is: 0x.*"
   - command: "mpu mtest 2"
     expected: "The value is: 0x.*"
   - command: "mpu status"
     expected: "MPU enabled"

.. code-block:: yaml
   :caption: testcase.yaml

   tests:
     shell.mpu:
       harness: shell
       harness_config:
         shell_commands_file: "test_shell.yml"

Custom File Location
********************

Specify a custom command file path:

.. code-block:: yaml

   tests:
     shell.custom:
       harness: shell
       harness_config:
         shell_commands_file: "config/custom_shell_tests.yml"

The path is relative to the test directory.

Output Validation
*****************

The ``expected`` field supports Python regular expressions, allowing flexible output
matching:

**Exact match:**

.. code-block:: yaml

   - command: "device list"
     expected: "uart@40001000"

**Pattern match:**

.. code-block:: yaml

   - command: "kernel uptime"
     expected: "Uptime: [0-9]+ ms"

**Multiple alternatives:**

.. code-block:: yaml

   - command: "log status"
     expected: "(enabled|active|running)"

**Partial match:**

.. code-block:: yaml

   - command: "version"
     expected: ".*Zephyr.*"  # Matches if "Zephyr" appears anywhere

**No validation (logging only):**

.. code-block:: yaml

   - command: "help"
     # No expected field - output is logged but not validated

Use Cases
*********

The shell harness is ideal for:

**Command Interface Testing**
    Verify shell commands work correctly and produce expected output.

**Shell Subsystem Validation**
    Test shell infrastructure, command parsing, and output handling.

**Utility Function Testing**
    Validate utilities and tools exposed through the shell interface.

**Integration Testing**
    Test interactions between shell and other subsystems.

**Regression Testing**
    Ensure shell commands continue to work across code changes.

**Documentation Validation**
    Verify shell command examples in documentation produce correct output.

Complete Example
****************

Here's a comprehensive example testing kernel shell commands:

.. code-block:: yaml

   common:
     tags: shell kernel
   tests:
     shell.kernel.commands:
       harness: shell
       harness_config:
         shell_commands:
           # Version information
           - command: "kernel version"
             expected: "Zephyr version [0-9]+\\.[0-9]+\\.[0-9]+"
           
           # Uptime check
           - command: "kernel uptime"
             expected: "Uptime: [0-9]+ ms"
           
           # Thread listing
           - command: "kernel threads"
             expected: ".*idle.*"
           
           # Cycle count
           - command: "kernel cycles"
             expected: "cycles: [0-9]+ hw cycles"
           
           # Reboot (no validation, just execute)
           - command: "kernel reboot warm"

Integration with Pytest
************************

The shell harness utilizes the pytest harness infrastructure, which means:

- You can combine shell testing with custom pytest code
- Standard pytest fixtures are available
- Test results integrate with pytest reporting
- You can use pytest command-line options (via ``pytest_args``)

Advanced configuration combining shell and pytest:

.. code-block:: yaml

   tests:
     shell.advanced:
       harness: shell
       harness_config:
         pytest_dut_scope: session
         pytest_args:
           - '--log-level=DEBUG'
         shell_commands:
           - command: "device list"
             expected: ".*uart.*"

Best Practices
**************

1. **Use specific patterns**: Make regex patterns specific enough to catch errors but
   flexible enough to handle minor output variations.

2. **Test incrementally**: Start with basic commands and gradually add more complex
   scenarios.

3. **Handle timing**: Some commands may take time to execute. Ensure adequate timeouts
   are configured.

4. **Separate concerns**: Use different test scenarios for different shell subsystems
   or features.

5. **Document expectations**: Comment your test configuration to explain what each
   command validates.

6. **Organize commands**: For complex test suites, use ``shell_commands_file`` to
   keep test configuration clean.

7. **Validate errors**: Test error cases by verifying error messages appear correctly.

8. **Test help text**: Include commands that verify help text is available and correct.

Example Error Testing
*********************

Test that invalid commands produce appropriate errors:

.. code-block:: yaml

   shell_commands:
     - command: "nonexistent_command"
       expected: "command not found"
     
     - command: "kernel version --invalid-flag"
       expected: "(invalid|unknown|error)"

Example Help Testing
********************

Validate help text availability:

.. code-block:: yaml

   shell_commands:
     - command: "help"
       expected: "Available commands:"
     
     - command: "kernel -h"
       expected: "kernel.*commands:"

Requirements
************

To use the shell harness:

1. **Shell subsystem enabled**: Test application must have Zephyr shell configured
   (``CONFIG_SHELL=y``)

2. **Shell backend**: Appropriate shell backend must be configured (e.g., UART shell
   via ``CONFIG_SHELL_BACKEND_SERIAL=y``)

3. **Commands available**: Shell commands being tested must be registered and available

4. **Serial connection**: For hardware testing, a working serial connection is required

Limitations
***********

- Commands execute sequentially; no parallel execution
- Limited to text-based output validation
- Cannot validate visual or graphical shell output
- Timing-dependent tests may need careful configuration
- Complex interactive scenarios may be difficult to automate

Troubleshooting
***************

**Commands not executing**
    - Verify shell is enabled in application configuration
    - Check serial connection and baud rate
    - Ensure shell prompt is appearing

**Output not matching**
    - Check regex syntax in expected patterns
    - Use ``.*`` to match variable portions
    - Log output without validation first to see actual format

**Timeout issues**
    - Some commands may take longer; adjust pytest timeout settings
    - Check device is responsive and not hung

**False failures**
    - Ensure patterns account for slight output variations
    - Check for platform-specific output differences

See Also
********

- :ref:`twister_pytest_harness` - Pytest harness documentation (shell uses pytest)
- :ref:`twister_console_harness` - For simpler output validation without interaction
- :ref:`twister_harnesses` - Overview of all available harnesses
- :ref:`twister_script` - Main Twister documentation
- :ref:`shell_api` - Zephyr shell subsystem documentation
