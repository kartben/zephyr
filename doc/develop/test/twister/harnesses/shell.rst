.. _twister_shell_harness:

Shell Harness
#############

Overview
********

The ``shell`` harness provides a specialized way to test Zephyr shell functionality by
executing shell commands and validating their output. It builds on top of the pytest
framework and the pytest harness, offering a convenient configuration-driven approach
to shell testing.

The shell harness is particularly useful for testing shell command implementations,
verifying shell subsystem behavior, and ensuring that shell commands produce expected
output across different platforms and configurations.

When to Use Shell Harness
==========================

Use the shell harness when:

* Testing Zephyr shell commands and their output
* Verifying shell subsystem functionality
* Testing command-line interfaces in your application
* You need to execute a series of shell commands in sequence
* You want a simpler alternative to writing full pytest tests for basic shell validation
* You need to verify both the presence and format of shell command output

The shell harness is ideal for:

* Validating that shell commands are registered correctly
* Testing command argument parsing
* Verifying command help text and usage information
* Testing shell command output formatting
* Ensuring shell commands work correctly across different platforms

How It Works
============

The shell harness:

1. Utilizes the pytest framework (via the pytest harness)
2. Provides a simplified YAML-based configuration for shell testing
3. Automatically executes specified shell commands
4. Validates command output against expected patterns using regular expressions
5. Reports results through Twister's standard reporting mechanisms

This allows you to test shell functionality without writing Python test code, while still
benefiting from pytest's robust test execution and reporting capabilities.

Configuration Options
*********************

The shell harness is configured in the test's YAML file under ``harness_config``.

shell_commands: <list of command/expected pairs> (default empty)
    Specifies a list of shell commands to execute and their expected output patterns.
    Each entry in the list can have:

    * ``command``: The shell command to execute (required)
    * ``expected``: Regular expression pattern for expected output (optional)

    If ``expected`` is not provided, the command is executed and its output is logged,
    but no validation is performed. This is useful for commands that modify state or
    for debugging.

    Example configuration:

    .. code-block:: yaml

        harness_config:
          shell_commands:
            - command: "kernel cycles"
              expected: "cycles: .* hw cycles"
            - command: "kernel version"
              expected: "Zephyr version .*"
            - command: "kernel sleep 100"
              # No expected output - just execute the command

    The ``expected`` field supports full regular expression syntax, allowing flexible
    pattern matching.

shell_commands_file: <string> (default empty)
    Specifies a YAML file containing test parameters and shell commands to execute.
    This is useful for:

    * Sharing command definitions across multiple test scenarios
    * Organizing large sets of shell commands
    * Maintaining test data separately from test configuration

    The file should contain a YAML list with the same structure as ``shell_commands``:

    .. code-block:: yaml

        # test_shell_commands.yml
        - command: "mpu mtest 1"
          expected: "The value is: 0x.*"
        - command: "mpu mtest 2"
          expected: "The value is: 0x.*"
        - command: "help"
          expected: "Available commands:"

    Then reference it in your test configuration:

    .. code-block:: yaml

        harness_config:
          shell_commands_file: test_shell_commands.yml

    .. note::
       If no file is specified and ``shell_commands`` is not provided, the shell harness
       will look for a default file named ``test_shell.yml`` in the test directory.

    .. tip::
       If both ``shell_commands`` and ``shell_commands_file`` are specified,
       ``shell_commands`` takes precedence and ``shell_commands_file`` is ignored.

Example Configurations
**********************

Basic Shell Testing
===================

Simple shell harness configuration testing basic kernel commands:

.. code-block:: yaml

    tests:
      sample.shell.basic:
        harness: shell
        harness_config:
          shell_commands:
            - command: "kernel version"
              expected: "Zephyr version [0-9]+\\.[0-9]+\\.[0-9]+"
            - command: "kernel uptime"
              expected: "Uptime: [0-9]+ ms"
        tags: shell

This configuration:

* Tests the ``kernel version`` command and validates the version format
* Tests the ``kernel uptime`` command and validates the output format
* Uses regular expressions to match variable parts of the output

Testing Multiple Subsystems
============================

Testing shell commands across different subsystems:

.. code-block:: yaml

    tests:
      shell.subsystems.comprehensive:
        harness: shell
        harness_config:
          shell_commands:
            # Kernel commands
            - command: "kernel version"
              expected: "Zephyr version"
            - command: "kernel threads"
              expected: ".*main.*"

            # Device commands
            - command: "device list"
              expected: "devices:"

            # Help system
            - command: "help"
              expected: "Available commands:"
            - command: "kernel -h"
              expected: "kernel - Kernel commands"

Using External Command File
============================

Organizing commands in a separate file for reusability:

.. code-block:: yaml

    # testcase.yaml
    tests:
      shell.commands.standard:
        harness: shell
        harness_config:
          shell_commands_file: standard_shell_tests.yml

    # standard_shell_tests.yml
    - command: "help"
      expected: "Available commands:"

    - command: "clear"
      # No expected output

    - command: "history"
      expected: ".*help.*"  # Should show previous 'help' command

    - command: "kernel version"
      expected: "Zephyr version"

    - command: "kernel uptime"
      expected: "Uptime:"

Platform-Specific Commands
===========================

Testing commands that may have platform-specific output:

.. code-block:: yaml

    tests:
      shell.platform.specific:
        harness: shell
        platform_allow: qemu_x86 native_sim
        harness_config:
          shell_commands:
            - command: "kernel reboot cold"
              # Platform will reboot - no output expected
            - command: "kernel cpustats"
              expected: "(CPU|cpu).*(usage|utilization)"

Commands Without Validation
============================

Executing commands for state setup without output validation:

.. code-block:: yaml

    tests:
      shell.state.setup:
        harness: shell
        harness_config:
          shell_commands:
            # Setup commands - just execute, don't validate
            - command: "log enable debug module_name"
            - command: "pwm cycles channel1 1000"
            - command: "gpio conf GPIO_0 0 out"

            # Validation commands
            - command: "log status"
              expected: "module_name.*debug"
            - command: "pwm info channel1"
              expected: "period: 1000"

Writing Effective Shell Tests
******************************

Best Practices
==============

1. **Use specific patterns**: Make regex patterns specific enough to catch errors
   but flexible enough to handle minor variations in output format.

   .. code-block:: yaml

       # Too specific - might break with formatting changes
       - command: "kernel version"
         expected: "Zephyr version 3.5.0"

       # Better - flexible but validates key information
       - command: "kernel version"
         expected: "Zephyr version [0-9]+\\.[0-9]+\\.[0-9]+"

2. **Test incrementally**: Start with basic commands and gradually add more complex ones.

3. **Order matters**: If commands depend on previous commands (e.g., setup then query),
   list them in the correct order.

4. **Handle optional output**: Use regex quantifiers (``?``, ``*``) for optional parts
   of output.

5. **Test help text**: Always include tests for command help text to ensure documentation
   is accessible through the shell.

6. **Escape special regex characters**: Remember to escape regex special characters in
   expected patterns (e.g., ``.``, ``*``, ``+``, ``?``).

7. **Use case-insensitive matching when appropriate**: Some output may vary in case.
   Use regex flags or patterns that handle both cases.

8. **Group related commands**: Use external command files to group related tests and
   make them reusable across test scenarios.

Common Patterns
===============

Here are some common regex patterns useful for shell testing:

.. code-block:: yaml

    # Match version numbers
    - command: "version"
      expected: "v[0-9]+\\.[0-9]+\\.[0-9]+"

    # Match hex addresses
    - command: "memory dump 0x1000"
      expected: "0x[0-9a-fA-F]+"

    # Match any number
    - command: "sensor get"
      expected: "temperature: [0-9]+\\.[0-9]+"

    # Match multiple lines (use .* for flexibility)
    - command: "device list"
      expected: ".*uart.*gpio.*i2c.*"

    # Match either/or
    - command: "status"
      expected: "(ready|active|running)"

    # Match with word boundaries
    - command: "kernel threads"
      expected: "\\bidle\\b"

Error Handling
==============

Test shell error conditions and error messages:

.. code-block:: yaml

    tests:
      shell.error.handling:
        harness: shell
        harness_config:
          shell_commands:
            # Invalid command
            - command: "nonexistent_command"
              expected: "(unknown command|command not found)"

            # Invalid arguments
            - command: "kernel"
              expected: "(invalid|missing|required).*(argument|parameter)"

            # Permission/capability errors
            - command: "restricted_operation"
              expected: "(permission denied|not allowed|unauthorized)"

Testing Command Output Format
==============================

Validate that commands produce properly formatted output:

.. code-block:: yaml

    tests:
      shell.format.validation:
        harness: shell
        harness_config:
          shell_commands:
            # Table format
            - command: "kernel threads"
              expected: ".*thread.*priority.*state.*"

            # List format
            - command: "device list"
              expected: "- .*\\n- .*"  # Items prefixed with dash

            # JSON-like format
            - command: "log status"
              expected: "\\{.*\\}"

Running Shell Tests
********************

Running with Twister
====================

Execute shell tests through Twister like any other test:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         $ ./scripts/twister -p native_sim -T tests/subsys/shell

   .. group-tab:: Windows

      .. code-block:: bat

         python .\scripts\twister -p native_sim -T tests\subsys\shell

Running Specific Shell Tests
=============================

To run a specific shell test scenario:

.. code-block:: bash

    $ ./scripts/twister -p native_sim -s tests/subsys/shell/shell_module -t shell.test.specific

Debugging Shell Tests
*********************

When shell tests fail:

1. **Check the expected pattern**: Verify your regex pattern is correct and matches
   the actual output format.

2. **Review actual output**: Look at the test logs to see what the shell actually
   produced versus what was expected.

3. **Test regex separately**: Use a regex testing tool to validate your patterns
   against sample output.

4. **Enable verbose logging**: Run Twister with ``-v`` to see more detailed output
   about test execution.

5. **Run commands manually**: Flash the device and manually execute shell commands
   to see their actual output.

6. **Check for timing issues**: Some commands may take time to complete. Ensure
   sufficient time is allowed for command execution.

7. **Verify shell is ready**: Ensure the shell prompt appears before commands are
   sent. Some platforms may have longer boot times.

Common Issues and Solutions
****************************

**Issue**: Test fails even though output looks correct
    **Solution**: Check for extra whitespace, escape characters, or differences in
    line endings. Make regex patterns flexible with ``\\s*`` for optional whitespace.

**Issue**: Commands timing out
    **Solution**: Some platforms or configurations may need more time for shell
    initialization. Adjust test timeout settings or add delay between commands.

**Issue**: Output format differs across platforms
    **Solution**: Use more flexible regex patterns that match the essential information
    rather than exact formatting. Consider platform-specific test scenarios if needed.

**Issue**: Shell commands not found
    **Solution**: Verify that the shell commands are actually registered in your
    application configuration. Check Kconfig options and command registration.

**Issue**: Inconsistent test results
    **Solution**: Ensure commands don't have side effects that affect subsequent
    commands. Consider whether commands are stateful or platform-dependent.

Integration with Other Harnesses
*********************************

The shell harness can be combined with other test configurations:

.. code-block:: yaml

    tests:
      shell.with.fixtures:
        harness: shell
        harness_config:
          shell_commands:
            - command: "sensor get"
              expected: "temperature: .*"
          fixture: sensor_board

      shell.with.filters:
        harness: shell
        platform_allow: native_sim qemu_x86
        harness_config:
          shell_commands_file: shell_tests.yml

      shell.with.dependencies:
        harness: shell
        depends_on: console uart
        harness_config:
          shell_commands:
            - command: "help"
              expected: "Available commands:"

Limitations
***********

Be aware of these limitations when using the shell harness:

1. **Sequential execution only**: Commands are executed in order. No parallel execution
   or complex control flow.

2. **Limited interaction**: Cannot handle complex interactive scenarios that require
   conditional responses based on output.

3. **No state management**: Cannot easily maintain complex state across commands
   beyond simple sequential execution.

4. **Pattern matching only**: Output validation is limited to regex pattern matching.
   Complex validation logic requires writing pytest tests instead.

5. **Timing assumptions**: Assumes commands complete within reasonable time. Very long
   operations might need different approaches.

For more complex shell testing scenarios, consider using the pytest harness with custom
shell test code.

Additional Resources
********************

* :ref:`Zephyr Shell documentation <shell_api>`
* :ref:`Pytest harness documentation <twister_pytest_harness>`
* :ref:`Shell subsystem documentation <shell_label>`
