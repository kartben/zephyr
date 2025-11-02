.. _twister_robot_harness:

Robot Harness
#############

Overview
********

The ``robot`` harness enables execution of `Robot Framework <https://robotframework.org/>`_
test suites in the `Renode <https://renode.io/>`_ simulation framework. Robot Framework is
a generic test automation framework that uses keyword-driven testing approach, allowing you
to write tests in a human-readable format.

The robot harness is specifically designed for running interactive test scenarios in Renode
simulation, providing a powerful way to test embedded systems without requiring physical
hardware. Tests can be written using natural language keywords, making them accessible to
both developers and non-technical stakeholders.

When to Use Robot Harness
==========================

Use the robot harness when:

* Testing applications in the Renode simulation environment
* You want to write tests in a human-readable, keyword-driven format
* You need to test complex interactive scenarios with the simulated device
* You want to leverage Renode's peripheral simulation capabilities
* You need to share test scenarios with non-developers who can read and understand them
* You're testing hardware-specific functionality that Renode can simulate

The robot harness is particularly useful for:

* End-to-end system testing in simulation
* Testing hardware peripherals and interactions
* Validating communication protocols
* Testing firmware behavior across different simulated hardware configurations
* Creating automated test scenarios that are self-documenting

Configuration Options
*********************

The robot harness is configured in the test's YAML file under ``harness_config``.

robot_testsuite: <robot file path> (required)
    Specifies one or more paths to files containing Robot Framework test suites to run.

    This option can specify:

    * A single robot file path (as a string)
    * Multiple robot file paths (as a YAML list)

    Single test suite example:

    .. code-block:: yaml

        tests:
          robot.example:
            harness: robot
            harness_config:
              robot_testsuite: test_suite.robot

    Multiple test suites example:

    .. code-block:: yaml

        tests:
          robot.example:
            harness: robot
            harness_config:
              robot_testsuite:
                - test_suite_1.robot
                - test_suite_2.robot
                - test_suite_n.robot

    .. note::
       When multiple test suites are specified, they are executed in the order listed.
       All suites must pass for the overall test scenario to pass.

robot_option: <robot option> (optional)
    One or more command-line options to pass to the Robot Framework when executing tests.
    These options allow you to control Robot Framework's behavior and customize test execution.

    Common Robot Framework options include:

    * ``--exclude <tag>``: Exclude tests with specific tags
    * ``--include <tag>``: Include only tests with specific tags
    * ``--variable <name:value>``: Set variables for the test run
    * ``--stop-on-error``: Stop test execution on first error
    * ``--loglevel <level>``: Set the logging level
    * ``--outputdir <path>``: Specify output directory for reports

    Example with multiple options:

    .. code-block:: yaml

        tests:
          robot.example:
            harness: robot
            harness_config:
              robot_testsuite: test_suite.robot
              robot_option:
                - --exclude tag
                - --stop-on-error
                - --variable PLATFORM:hifive1

    For a complete list of Robot Framework options, see the
    `Robot Framework User Guide <https://robotframework.org/robotframework/latest/RobotFrameworkUserGuide.html>`_.

Example Configurations
**********************

Basic Robot Harness
===================

Minimal configuration for running a single Robot Framework test suite:

.. code-block:: yaml

    tests:
      sample.shell.shell_module.robot:
        harness: robot
        harness_config:
          robot_testsuite: shell_module.robot
        tags: shell

Multiple Test Suites
====================

Running multiple test suites sequentially:

.. code-block:: yaml

    tests:
      peripheral.test.comprehensive:
        harness: robot
        harness_config:
          robot_testsuite:
            - tests/uart_test.robot
            - tests/gpio_test.robot
            - tests/i2c_test.robot

Advanced Configuration with Options
====================================

Using Robot Framework options to control test execution:

.. code-block:: yaml

    tests:
      integration.test.advanced:
        harness: robot
        harness_config:
          robot_testsuite: integration_suite.robot
          robot_option:
            - --exclude slow
            - --include critical
            - --variable TIMEOUT:60
            - --loglevel DEBUG
            - --stop-on-error

Writing Robot Framework Tests
******************************

Robot Framework uses a keyword-driven approach where tests are written using high-level
keywords that describe test actions in natural language.

Basic Test Structure
====================

A Robot Framework test file (``.robot``) typically contains:

* **Settings**: Imports, library declarations, and test setup/teardown
* **Variables**: Test data and configuration
* **Keywords**: Reusable test steps (optional)
* **Test Cases**: The actual tests

Example Robot test file:

.. code-block:: robotframework

    *** Settings ***
    Suite Setup       Setup
    Suite Teardown    Teardown
    Test Setup        Reset Emulation
    Resource          ${RENODEKEYWORDS}

    *** Variables ***
    ${UART}                 sysbus.uart0
    ${URI}                  @https://dl.antmicro.com/projects/renode

    *** Test Cases ***
    Should Print Hello World
        Create Machine
        Execute Command    sysbus LoadELF ${CURDIR}/hello_world.elf
        Start Emulation

        Wait For Line On Uart   Hello World!    timeout=5

    Should Handle Shell Commands
        Create Machine
        Execute Command    sysbus LoadELF ${CURDIR}/shell_app.elf
        Start Emulation

        Wait For Prompt On Uart   uart:~$
        Write Line To Uart        help
        Wait For Line On Uart     Available commands:

Available Keywords
==================

Renode provides a rich set of keywords for test automation. Common categories include:

**Machine Control**
    * Create Machine
    * Start Emulation
    * Pause Emulation
    * Reset Emulation

**UART Communication**
    * Wait For Line On Uart
    * Wait For Prompt On Uart
    * Write Line To Uart
    * Write Char On Uart

**Command Execution**
    * Execute Command
    * Execute Script

**Assertions and Checks**
    * Should Contain Line
    * Should Not Contain Line

For a comprehensive list of available keywords and their usage, see the
`Renode testing documentation <https://renode.readthedocs.io/en/latest/introduction/testing.html>`_.

Running Robot Tests
*******************

Running with Twister
====================

To execute Robot Framework tests through Twister:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         $ ./scripts/twister --platform hifive1 --test samples/subsys/shell/shell_module/sample.shell.shell_module.robot

   .. group-tab:: Windows

      .. code-block:: bat

         python .\scripts\twister --platform hifive1 --test samples/subsys/shell/shell_module/sample.shell.shell_module.robot

Running Directly with Renode
=============================

For development and debugging, you can run Robot tests directly with Renode:

.. code-block:: bash

    $ renode-test test_suite.robot

This bypasses Twister and runs the test directly in Renode, which can be useful for
rapid iteration during test development.

Best Practices
**************

1. **Use descriptive test names**: Test case names should clearly describe what is being
   tested. Robot Framework allows natural language names with spaces.

   .. code-block:: robotframework

       *** Test Cases ***
       Should Initialize UART With Correct Baud Rate
           # Test implementation

2. **Keep tests focused**: Each test case should verify one specific behavior or aspect
   of functionality.

3. **Use variables for configuration**: Define configurable values as variables rather
   than hardcoding them in test steps.

   .. code-block:: robotframework

       *** Variables ***
       ${TIMEOUT}    10
       ${EXPECTED}   Boot successful

       *** Test Cases ***
       Should Boot Successfully
           Wait For Line On Uart   ${EXPECTED}   timeout=${TIMEOUT}

4. **Create custom keywords**: Extract common sequences into custom keywords for reuse
   and better readability.

   .. code-block:: robotframework

       *** Keywords ***
       Setup Device
           Create Machine
           Execute Command    sysbus LoadELF ${APPLICATION}
           Start Emulation

       *** Test Cases ***
       Should Run Application
           Setup Device
           Wait For Line On Uart   Application started

5. **Use tags for organization**: Tag tests to enable selective execution and better
   organization.

   .. code-block:: robotframework

       *** Test Cases ***
       Basic Functionality Test
           [Tags]    basic    smoke
           # Test implementation

       Advanced Feature Test
           [Tags]    advanced    slow
           # Test implementation

6. **Add documentation**: Document test suites and test cases to explain their purpose
   and any special requirements.

   .. code-block:: robotframework

       *** Test Cases ***
       Should Handle Network Traffic
           [Documentation]    Verifies that the network stack can handle
           ...                incoming TCP connections and process data correctly.
           ...                Requires: Network peripheral simulation
           # Test implementation

7. **Handle timeouts appropriately**: Set realistic timeouts based on expected execution
   time. Too short timeouts cause false failures; too long delays failure detection.

8. **Clean up resources**: Use teardown keywords to ensure clean state between tests
   and after test suite completion.

9. **Use meaningful variable names**: Choose variable names that clearly indicate their
   purpose and content.

10. **Version control test files**: Keep Robot test files under version control alongside
    the code they test.

Test Organization
*****************

Structuring Test Suites
========================

For larger projects, organize Robot test files into a logical structure:

.. code-block:: none

    tests/
    ├── robot/
    │   ├── basic/
    │   │   ├── boot_test.robot
    │   │   └── uart_test.robot
    │   ├── peripherals/
    │   │   ├── gpio_test.robot
    │   │   ├── i2c_test.robot
    │   │   └── spi_test.robot
    │   └── integration/
    │       └── full_system_test.robot
    └── testcase.yaml

Reusable Resources
==================

Create resource files for shared keywords, variables, and setup/teardown procedures:

.. code-block:: robotframework

    # common_resources.robot
    *** Settings ***
    Resource          ${RENODEKEYWORDS}

    *** Variables ***
    ${COMMON_TIMEOUT}    30

    *** Keywords ***
    Common Setup
        Reset Emulation
        Create Machine

    Common Teardown
        Stop Emulation

Then import in test files:

.. code-block:: robotframework

    *** Settings ***
    Resource          common_resources.robot

    *** Test Cases ***
    My Test
        # Can use keywords and variables from common_resources.robot

Debugging Failed Tests
**********************

When Robot tests fail:

1. **Check the test log**: Robot Framework generates detailed HTML logs and reports.
   These are typically in the output directory and show exactly which keyword failed.

2. **Review UART output**: Check the captured UART communication to see what the device
   actually output versus what was expected.

3. **Verify timing**: Ensure timeouts are appropriate for the platform and test scenario.
   Simulation can be slower than expected.

4. **Check Renode setup**: Verify that the Renode platform configuration matches the
   target hardware being tested.

5. **Run tests individually**: If a test suite contains multiple tests, run them
   individually to isolate failures.

6. **Use logging**: Add logging statements to custom keywords to track execution flow.

7. **Verify test data**: Ensure any external files or data required by tests are present
   and correctly formatted.

Common Issues and Solutions
****************************

**Issue**: Tests timeout waiting for output
    **Solution**: Check that the application is actually producing the expected output.
    Verify UART configuration matches between Renode and application. Increase timeout
    if simulation is legitimately slow.

**Issue**: Renode fails to start
    **Solution**: Ensure Renode is properly installed and in the PATH. Check that
    platform configuration files are accessible and correctly formatted.

**Issue**: Tests pass locally but fail in CI
    **Solution**: Verify that CI environment has Renode installed and configured.
    Check for timing differences or resource constraints in CI environment.

**Issue**: Cannot find robot file
    **Solution**: Verify the path in ``robot_testsuite`` is correct relative to the
    test directory. Use absolute paths if needed.

**Issue**: Robot Framework options not taking effect
    **Solution**: Check syntax in ``robot_option`` list. Each option should be a
    separate list item. Verify options are valid for your Robot Framework version.

Additional Resources
********************

* `Robot Framework User Guide <https://robotframework.org/robotframework/latest/RobotFrameworkUserGuide.html>`_
* `Renode Documentation <https://renode.readthedocs.io/>`_
* `Renode Testing Guide <https://renode.readthedocs.io/en/latest/introduction/testing.html>`_
* `Robot Framework Standard Libraries <https://robotframework.org/robotframework/#standard-libraries>`_
