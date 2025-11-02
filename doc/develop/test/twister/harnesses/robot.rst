.. _twister_robot_harness:

Robot Harness
#############

The ``robot`` harness integrates Robot Framework test suites with Twister, enabling
human-readable, keyword-driven test automation. This harness is specifically designed
to work with the Renode simulation framework, allowing you to write expressive test
scenarios that combine device simulation with automated testing.

Overview
********

Robot Framework is a generic test automation framework that uses a keyword-driven approach,
making tests easy to read and write even for non-programmers. When combined with Renode's
simulation capabilities and Twister's device management, it provides a powerful platform
for testing embedded systems through simulation.

The robot harness executes Robot Framework test suites in the Renode simulator and reports
results back to Twister. This enables:

- Readable, maintainable test scenarios
- Simulation of hardware peripherals and interactions
- Complex test workflows with minimal coding
- Reusability of test keywords and resources

How It Works
************

When using the robot harness:

1. Twister builds the test application
2. Renode simulation is configured according to the platform definition
3. The Robot Framework test suite is executed, controlling Renode
4. Test keywords interact with the simulated device
5. Results are collected and reported by Twister

For more information on writing Robot tests for Zephyr, see the
:ref:`Robot Framework Tests <twister_robot_tests>` section.

Configuration Options
*********************

The robot harness requires configuration to specify which test suites to run:

robot_testsuite: <robot file path> (default empty)
    Specifies one or more paths to Robot Framework test suite files (.robot files).
    Multiple test suites can be specified as a list.

robot_option: <robot option> (default empty)
    Additional command-line options to pass to the Robot Framework executor.
    Can be specified multiple times for multiple options.

Basic Example
*************

Here's a simple test configuration using the robot harness:

.. code-block:: yaml

   tests:
     robot.example:
       harness: robot
       harness_config:
         robot_testsuite: test_suite.robot

Multiple Test Suites
*********************

You can specify multiple test suite files to run:

.. code-block:: yaml

   tests:
     robot.comprehensive:
       harness: robot
       harness_config:
         robot_testsuite:
           - tests/test_basic.robot
           - tests/test_advanced.robot
           - tests/test_integration.robot

With Custom Options
*******************

This example shows passing additional options to Robot Framework:

.. code-block:: yaml

   tests:
     robot.example.filtered:
       harness: robot
       harness_config:
         robot_testsuite: test_suite.robot
         robot_option:
           - --exclude tag_to_skip
           - --stop-on-error
           - --loglevel DEBUG

Common Robot Framework Options
*******************************

Some useful Robot Framework options that can be specified via ``robot_option``:

``--include <tag>``
    Execute only tests with the specified tag

``--exclude <tag>``
    Skip tests with the specified tag

``--stop-on-error``
    Stop execution immediately if any test fails

``--loglevel <TRACE|DEBUG|INFO|WARN|ERROR>``
    Set the threshold level for logging

``--variable <name:value>``
    Set variables for test execution

``--outputdir <path>``
    Specify where to write output files

Writing Robot Tests
*******************

Robot Framework tests for Zephyr typically interact with Renode simulation. Here's
a simple example of a Robot test file:

.. code-block:: robotframework

   *** Settings ***
   Suite Setup       Setup
   Suite Teardown    Teardown
   Resource          ${RENODEKEYWORDS}

   *** Test Cases ***
   Should Print Hello World
       Create Terminal Tester    sysbus.uart
       Start Emulation
       Wait For Line On Uart     Hello World from Zephyr!

   Should Respond To Shell Command
       Create Terminal Tester    sysbus.uart  
       Start Emulation
       Wait For Prompt On Uart   uart:~$
       Write Line To Uart        kernel version
       Wait For Line On Uart     Zephyr version

Use Cases
*********

The robot harness is ideal for:

**Simulation Testing**
    Testing applications in Renode simulation with complex interaction scenarios.

**Acceptance Testing**
    Writing high-level acceptance tests that non-developers can read and understand.

**Interactive Scenarios**
    Tests that involve sending commands and validating responses over time.

**Hardware Peripheral Testing**
    Simulating peripheral interactions (UART, SPI, I2C, etc.) and validating behavior.

**Regression Testing**
    Maintaining a suite of readable regression tests that document expected behavior.

Best Practices
**************

1. **Organize with keywords**: Create reusable keywords for common test operations to
   keep tests DRY (Don't Repeat Yourself).

2. **Use descriptive test names**: Make test case names clear about what they verify.

3. **Structure test files**: Organize tests logically into suite files by feature or
   component.

4. **Leverage tags**: Use tags to categorize tests for selective execution.

5. **Document test intent**: Use comments and documentation strings to explain test
   purpose and expected behavior.

6. **Keep tests focused**: Each test case should verify one specific behavior or requirement.

Requirements
************

To use the robot harness:

- Renode must be installed and available in the system PATH
- Robot Framework must be installed (typically via ``pip install robotframework``)
- The platform being tested must have a Renode simulation configuration
- Test suites must be compatible with Renode's Robot keywords

Limitations
***********

- The robot harness currently only works with Renode simulation
- Hardware testing is not supported through this harness
- Test execution is limited by Renode's simulation capabilities

See Also
********

- :ref:`twister_robot_tests` - Detailed information on writing Robot Framework tests
- :ref:`twister_harnesses` - Overview of all available harnesses
- :ref:`twister_script` - Main Twister documentation
- `Robot Framework documentation <https://robotframework.org/>`_ - Official Robot Framework docs
- `Renode documentation <https://renode.readthedocs.io/>`_ - Renode testing guide
