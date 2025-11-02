.. _twister_robot_tests:

Robot Framework Tests
######################

Zephyr supports `Robot Framework <https://robotframework.org/>`_ as a solution for
automated testing. Robot Framework provides a keyword-driven approach to writing
test cases in a human-readable format, making tests accessible to both technical
and non-technical team members.

Overview
********

Robot Framework tests allow you to express interactive test scenarios in plain text
format and execute them in simulation or against hardware. The integration with
Zephyr currently supports running Robot tests in the
`Renode <https://renode.io/>`_ simulation framework, which provides accurate
hardware simulation for embedded systems.

Robot tests are particularly valuable for:

- Acceptance testing with readable test cases
- Complex interaction scenarios
- Test cases that non-developers need to read or write
- Regression testing with clear documentation of expected behavior
- Integration testing across multiple components

Using Robot Framework with Twister
***********************************

To execute a Robot test suite with Twister, use the ``robot`` harness and specify
the test:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         $ ./scripts/twister --platform hifive1 --test samples/subsys/shell/shell_module/sample.shell.shell_module.robot

   .. group-tab:: Windows

      .. code-block:: bat

         python .\scripts\twister --platform hifive1 --test samples/subsys/shell/shell_module/sample.shell.shell_module.robot

Twister will:

1. Build the test application
2. Start Renode simulation
3. Execute the Robot Framework test suite
4. Report results

Test Configuration
==================

Robot tests are configured in ``testcase.yaml`` or ``sample.yaml`` files using
the ``robot`` harness:

.. code-block:: yaml

   tests:
     sample.shell.shell_module.robot:
       harness: robot
       harness_config:
         robot_testsuite: test_shell.robot

For detailed information on configuring the robot harness, see
:ref:`twister_robot_harness`.

Writing Robot Tests
*******************

Robot Framework tests consist of test cases written in a tabular format using
keywords. The tests interact with the simulated device through Renode's Robot
Framework keywords.

Basic Structure
===============

A typical Robot test file includes:

.. code-block:: robotframework

   *** Settings ***
   Suite Setup       Setup
   Suite Teardown    Teardown
   Resource          ${RENODEKEYWORDS}

   *** Test Cases ***
   Test Shell Help Command
       Create Terminal Tester    sysbus.uart
       Start Emulation
       Wait For Line On Uart     Hello World
       Write Line To Uart        help
       Wait For Line On Uart     Available commands:

Key Components
==============

**Settings Section**
    Defines setup, teardown, and imported resources. The ``${RENODEKEYWORDS}``
    resource provides Renode-specific keywords.

**Test Cases Section**
    Contains individual test cases with descriptive names and keyword-driven steps.

**Keywords**
    Actions that can be performed, such as starting emulation, sending UART data,
    or waiting for specific output.

Renode Keywords
***************

Renode provides specialized Robot Framework keywords for device interaction.
Common keywords include:

Terminal Testing
================

Create Terminal Tester    <peripheral_path>
    Creates a terminal tester for UART/serial communication.
    Example: ``Create Terminal Tester    sysbus.uart``

Write Line To Uart    <text>
    Sends text followed by newline to the UART.
    Example: ``Write Line To Uart    kernel version``

Wait For Line On Uart    <pattern>
    Waits for a line matching the pattern to appear on UART.
    Example: ``Wait For Line On Uart    Zephyr version.*``

Wait For Prompt On Uart    <prompt>
    Waits for a specific prompt string on UART.
    Example: ``Wait For Prompt On Uart    uart:~$``

Emulation Control
=================

Start Emulation
    Starts the Renode emulation.

Execute Command    <command>
    Executes a Renode monitor command.
    Example: ``Execute Command    sysbus.uart RecordToFile @uart.txt true``

Create Log Tester    <timeout>
    Creates a tester for log output with specified timeout.

Example Test Cases
******************

Simple UART Test
================

.. code-block:: robotframework

   *** Test Cases ***
   Should Print Hello World
       Create Terminal Tester    sysbus.uart
       Start Emulation
       Wait For Line On Uart     Hello World from Zephyr!

Interactive Shell Test
======================

.. code-block:: robotframework

   *** Test Cases ***
   Should Respond To Shell Commands
       Create Terminal Tester    sysbus.uart
       Start Emulation
       Wait For Prompt On Uart   uart:~$
       
       # Test kernel version command
       Write Line To Uart        kernel version
       Wait For Line On Uart     Zephyr version [0-9]+\\.[0-9]+\\.[0-9]+
       
       # Test help command
       Write Line To Uart        help
       Wait For Line On Uart     Available commands:
       
       # Test device list command
       Write Line To Uart        device list
       Wait For Line On Uart     uart@.*

Multiple Test Cases
===================

.. code-block:: robotframework

   *** Test Cases ***
   Should Initialize Shell
       Create Terminal Tester    sysbus.uart
       Start Emulation
       Wait For Prompt On Uart   uart:~$

   Should Execute Kernel Commands
       [Setup]                   Test Setup
       Write Line To Uart        kernel version
       Wait For Line On Uart     Zephyr
       Write Line To Uart        kernel uptime
       Wait For Line On Uart     Uptime:

   Should List Devices
       Write Line To Uart        device list
       Wait For Line On Uart     devices:
       Wait For Line On Uart     uart@

   *** Keywords ***
   Test Setup
       Create Terminal Tester    sysbus.uart
       Start Emulation
       Wait For Prompt On Uart   uart:~$

Resources and Documentation
****************************

Robot Framework Resources
==========================

For the complete list of keywords provided by Robot Framework:

- `Robot Framework User Guide <https://robotframework.org/robotframework/latest/RobotFrameworkUserGuide.html>`_
- `Robot Framework Standard Libraries <https://robotframework.org/robotframework/>`_
- `Robot Framework Keyword Documentation <https://robotframework.org/robotframework/latest/libraries/BuiltIn.html>`_

Renode Resources
================

Information on writing and running Robot Framework tests in Renode:

- `Renode Testing Guide <https://renode.readthedocs.io/en/latest/introduction/testing.html>`_
- `Renode Robot Framework Integration <https://renode.readthedocs.io/en/latest/tutorials/robot-framework.html>`_
- `Renode Keywords Source <https://github.com/renode/renode/tree/master/src/Renode/RobotFramework>`_

Extending Robot Framework
**************************

Robot Framework can be extended with custom keywords in several ways:

1. **In test suite files**: Define reusable keywords directly in ``.robot`` files
2. **External Python libraries**: Create Python modules with custom keywords
3. **XML-RPC (Renode approach)**: Dynamically add keywords via XML-RPC

For details on extending Robot Framework, see:

- `Extending Robot Framework <https://robotframework.org/robotframework/latest/RobotFrameworkUserGuide.html#extending-robot-framework>`_

Custom Keyword Example
======================

.. code-block:: robotframework

   *** Keywords ***
   Verify Shell Is Ready
       Create Terminal Tester    sysbus.uart
       Start Emulation
       Wait For Prompt On Uart   uart:~$

   Execute Shell Command
       [Arguments]               ${command}    ${expected_output}
       Write Line To Uart        ${command}
       Wait For Line On Uart     ${expected_output}

   *** Test Cases ***
   Should Execute Custom Commands
       Verify Shell Is Ready
       Execute Shell Command     kernel version     Zephyr
       Execute Shell Command     device list        devices:

Running Specific Tests
**********************

Running a Single Test Suite
============================

To run a single test suite instead of all tests:

.. code-block:: bash

   $ twister -p qemu_riscv32 -s tests/kernel/interrupt/arch.shared_interrupt

Running Specific Test Cases
============================

Robot Framework supports selecting specific tests using tags or patterns.
Configure this through the harness configuration:

.. code-block:: yaml

   tests:
     sample.test.filtered:
       harness: robot
       harness_config:
         robot_testsuite: test_suite.robot
         robot_option:
           - --include=smoke
           - --exclude=slow

Best Practices
**************

1. **Descriptive Names**: Use clear, descriptive names for test cases that
   explain what is being tested.

2. **Reusable Keywords**: Extract common operations into custom keywords to
   avoid duplication.

3. **Appropriate Timeouts**: Set timeouts that account for actual device behavior
   and simulation speed.

4. **Organized Structure**: Group related tests in the same suite file and use
   tags for categorization.

5. **Clear Assertions**: Make expected behavior explicit in test names and
   keywords.

6. **Documentation**: Include documentation strings for complex test cases and
   custom keywords.

7. **Small Test Cases**: Keep individual test cases focused on one aspect of
   functionality.

8. **Setup and Teardown**: Use suite and test setup/teardown to ensure clean
   test state.

Example: Well-Organized Test Suite
===================================

.. code-block:: robotframework

   *** Settings ***
   Documentation     Shell subsystem integration tests
   Suite Setup       Setup
   Suite Teardown    Teardown
   Resource          ${RENODEKEYWORDS}

   *** Variables ***
   ${UART}           sysbus.uart
   ${PROMPT}         uart:~$

   *** Keywords ***
   Initialize Shell
       Create Terminal Tester    ${UART}
       Start Emulation
       Wait For Prompt On Uart   ${PROMPT}

   Execute And Verify
       [Arguments]               ${command}    ${expected}
       Write Line To Uart        ${command}
       Wait For Line On Uart     ${expected}

   *** Test Cases ***
   Should Initialize Shell Successfully
       [Documentation]    Verify shell initializes and shows prompt
       [Tags]            smoke    initialization
       Initialize Shell

   Should Execute Help Command
       [Documentation]    Verify help command lists available commands
       [Tags]            smoke    shell
       [Setup]           Initialize Shell
       Execute And Verify    help    Available commands:

   Should Show Kernel Version
       [Documentation]    Verify kernel version command works
       [Tags]            kernel    version
       [Setup]           Initialize Shell
       Execute And Verify    kernel version    Zephyr version

   Should List Devices
       [Documentation]    Verify device list command shows devices
       [Tags]            device    list
       [Setup]           Initialize Shell
       Execute And Verify    device list    devices:

Troubleshooting
***************

**Test fails with "No keyword with name..."**
    - Ensure ``${RENODEKEYWORDS}`` resource is imported
    - Check keyword name spelling
    - Verify Renode installation includes Robot Framework support

**"Timeout waiting for..."**
    - Increase timeout in keyword or test configuration
    - Check if device/simulation is actually producing expected output
    - Verify UART/terminal configuration is correct

**Tests pass but output seems wrong**
    - Review actual vs. expected patterns
    - Check if patterns need escaping (regex special characters)
    - Use ``Log`` keyword to debug intermediate values

**Renode simulation issues**
    - Verify Renode platform configuration for the board
    - Check Renode logs for errors
    - Ensure test platform matches ``--platform`` argument

See Also
********

- :ref:`twister_robot_harness` - Robot harness configuration reference
- :ref:`twister_harnesses` - Overview of all available harnesses
- :ref:`twister_script` - Main Twister documentation
- `Robot Framework <https://robotframework.org/>`_ - Official Robot Framework site
- `Renode <https://renode.io/>`_ - Renode simulator homepage
