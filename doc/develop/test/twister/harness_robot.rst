.. _twister_harness_robot:

Robot Harness
#############

.. _twister_robot_harness:

Overview
********

The Robot harness integrates the powerful `Robot Framework <https://robotframework.org/>`_
with Twister for automated testing in the Renode simulation environment. Robot Framework
is a generic test automation framework that uses keyword-driven testing and provides
excellent readability for non-programmers.

This harness enables you to:

- Write human-readable test scenarios using natural language-like syntax
- Execute tests in the Renode simulation framework
- Leverage Robot Framework's extensive keyword libraries
- Create reusable test components and keywords
- Generate detailed test reports with screenshots and logs

The Robot harness is particularly well-suited for:

- **System-level testing** of complete applications
- **Integration scenarios** requiring complex device interactions
- **Acceptance testing** where test readability is important
- **Regression testing** with extensive test suites
- **Simulated hardware testing** in Renode

How It Works
************

The Robot harness workflow:

1. **Simulation Setup**: Renode simulation environment is configured and started
2. **Robot Execution**: Robot Framework test suites are executed
3. **Device Interaction**: Tests interact with the simulated device through Renode's API
4. **Result Collection**: Robot Framework generates detailed reports
5. **Status Reporting**: Pass/fail status is communicated back to Twister

Robot Framework provides keywords for device interaction, assertion checking, and
control flow, making tests readable and maintainable.

Configuration
*************

**robot_testsuite: <robot file path>** (default empty)
    Specifies the path(s) to Robot Framework test suite file(s). You can provide:
    
    - A single test suite file
    - Multiple test suite files as a list
    - Relative or absolute paths

**robot_option: <robot option>** (default empty)
    Additional command-line options to pass to Robot Framework. Can include:
    
    - ``--exclude tag``: Skip tests with specific tags
    - ``--stop-on-error``: Stop execution on first failure
    - ``--loglevel DEBUG``: Set logging verbosity
    - And any other Robot Framework CLI option

Basic Example
*************

Single Test Suite
=================

**testcase.yaml**:

.. code-block:: yaml

    tests:
      robot.example:
        harness: robot
        platform: hifive1
        harness_config:
          robot_testsuite: tests/shell.robot

**tests/shell.robot**:

.. code-block:: robotframework

    *** Settings ***
    Documentation    Test shell functionality
    Library          RenodeKeywords

    *** Test Cases ***
    Test Shell Help Command
        [Documentation]    Verify help command displays available commands
        Create Terminal Tester    sysbus.uart0
        Start Emulation
        Wait For Line On Uart      Shell>
        Write Line To Uart         help
        Wait For Line On Uart      Available commands

    Test Shell Version
        [Documentation]    Verify version command output
        Write Line To Uart         kernel version
        Wait For Line On Uart      Zephyr version

Multiple Test Suites
====================

.. code-block:: yaml

    tests:
      robot.comprehensive:
        harness: robot
        harness_config:
          robot_testsuite:
            - tests/shell.robot
            - tests/gpio.robot
            - tests/sensors.robot

With Robot Options
==================

.. code-block:: yaml

    tests:
      robot.example:
        harness: robot
        harness_config:
          robot_testsuite: tests/all_tests.robot
          robot_option:
            - --exclude slow
            - --stop-on-error
            - --loglevel DEBUG

Writing Robot Tests for Zephyr
*******************************

Robot Framework test files use a structured format with sections:

Test Structure
==============

.. code-block:: robotframework

    *** Settings ***
    Documentation    Test suite description
    Library          RenodeKeywords
    Suite Setup      Setup Actions
    Suite Teardown   Teardown Actions

    *** Variables ***
    ${TIMEOUT}       30s
    ${UART}          sysbus.uart0

    *** Test Cases ***
    Test Case Name
        [Documentation]    What this test verifies
        [Tags]             smoke  feature-xyz
        Test Keyword 1
        Test Keyword 2
        Validate Result

    *** Keywords ***
    Custom Keyword
        [Documentation]    Reusable test step
        Do Something
        Verify Something

Common Renode Keywords
======================

The Renode integration provides keywords for device interaction:

.. code-block:: robotframework

    *** Test Cases ***
    Comprehensive Example
        # Terminal/UART interaction
        Create Terminal Tester    sysbus.uart0
        
        # Start simulation
        Start Emulation
        
        # Wait for output
        Wait For Line On Uart      expected text    timeout=5s
        
        # Send commands
        Write Line To Uart         shell command
        
        # Check patterns
        Wait For Line On Uart      pattern.*regex    treatAsRegex=${True}
        
        # Control simulation
        Pause Emulation
        Resume Emulation

Test Case Example
=================

Here's a complete example testing a sensor application:

.. code-block:: robotframework

    *** Settings ***
    Documentation    HTS221 Temperature and Humidity Sensor Tests
    Library          RenodeKeywords
    Suite Setup      Setup Sensor Test
    Suite Teardown   Teardown Sensor Test

    *** Variables ***
    ${UART}              sysbus.uart0
    ${TEMP_MIN}          20.0
    ${TEMP_MAX}          25.0
    ${HUMIDITY_MIN}      40.0
    ${HUMIDITY_MAX}      60.0

    *** Test Cases ***
    Test Sensor Initialization
        [Documentation]    Verify sensor initializes correctly
        [Tags]             initialization  sensors
        Wait For Line On Uart    Sensor initialized    timeout=10s
        Wait For Line On Uart    Starting measurements

    Test Temperature Reading
        [Documentation]    Verify temperature readings are within range
        [Tags]             sensors  temperature
        ${output}=    Wait For Line On Uart    Temperature:    timeout=5s
        ${temp}=      Extract Temperature      ${output}
        Should Be True    ${temp} >= ${TEMP_MIN}
        Should Be True    ${temp} <= ${TEMP_MAX}

    Test Humidity Reading
        [Documentation]    Verify humidity readings are valid
        [Tags]             sensors  humidity
        ${output}=    Wait For Line On Uart    Humidity:    timeout=5s
        ${humidity}=  Extract Humidity         ${output}
        Should Be True    ${humidity} >= ${HUMIDITY_MIN}
        Should Be True    ${humidity} <= ${HUMIDITY_MAX}

    Test Continuous Operation
        [Documentation]    Verify sensor continues to produce readings
        [Tags]             stress  sensors
        FOR    ${i}    IN RANGE    10
            Wait For Line On Uart    Temperature:    timeout=5s
            Wait For Line On Uart    Humidity:       timeout=5s
        END

    *** Keywords ***
    Setup Sensor Test
        Create Terminal Tester    ${UART}
        Start Emulation

    Teardown Sensor Test
        Stop Emulation

    Extract Temperature
        [Arguments]    ${line}
        ${pattern}=    Set Variable    Temperature: ([0-9.]+)
        ${temp}=       Get Regexp Matches    ${line}    ${pattern}    1
        [Return]       ${temp[0]}

    Extract Humidity
        [Arguments]    ${line}
        ${pattern}=    Set Variable    Humidity: ([0-9.]+)
        ${hum}=        Get Regexp Matches    ${line}    ${pattern}    1
        [Return]       ${hum[0]}

Best Practices
**************

1. **Use Descriptive Names**: Test case names should clearly describe what is being tested

2. **Add Documentation**: Use ``[Documentation]`` to explain test purpose and expected behavior

3. **Tag Tests Appropriately**: Use ``[Tags]`` to categorize tests for selective execution

4. **Create Reusable Keywords**: Extract common operations into custom keywords in the
   ``*** Keywords ***`` section

5. **Use Variables**: Define constants in ``*** Variables ***`` section for easy maintenance

6. **Setup and Teardown**: Use Suite/Test Setup/Teardown for proper resource management

7. **Timeouts**: Always specify appropriate timeouts for ``Wait For Line On Uart``

8. **Regex When Needed**: Use ``treatAsRegex=${True}`` for flexible pattern matching

9. **Comments**: Add comments to explain complex logic:

   .. code-block:: robotframework

       # This section validates the boot sequence
       Wait For Line On Uart    Booting Zephyr

10. **Organize by Feature**: Group related test cases and create separate robot files
    for different features

Working with Renode
*******************

For more information on Renode-specific keywords and capabilities:

- `Renode Documentation <https://renode.readthedocs.io/>`_
- `Renode Testing <https://renode.readthedocs.io/en/latest/introduction/testing.html>`_
- `Robot Framework User Guide <https://robotframework.org/robotframework/latest/RobotFrameworkUserGuide.html>`_

Running Robot Tests
*******************

Execute Robot tests using Twister:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         ./scripts/twister --platform hifive1 \
           --test samples/subsys/shell/shell_module/sample.shell.shell_module.robot

   .. group-tab:: Windows

      .. code-block:: bat

         python .\scripts\twister --platform hifive1 ^
           --test samples/subsys/shell/shell_module/sample.shell.shell_module.robot

Viewing Reports
***************

Robot Framework generates detailed HTML reports:

- **report.html**: High-level test execution report
- **log.html**: Detailed log with keyword-level information
- **output.xml**: Machine-readable output for further processing

These files are generated in the test build directory after execution.

Troubleshooting
***************

Common Issues
=============

**Robot Framework Not Found**
    Ensure Robot Framework is installed:
    
    .. code-block:: bash
    
        pip install robotframework

**Renode Not Available**
    Install and configure Renode according to Zephyr's documentation.

**Test Suite Not Found**
    Verify the path in ``robot_testsuite`` is correct relative to the test directory.

**Timeout Errors**
    - Increase timeout values in ``Wait For Line On Uart`` calls
    - Check if expected output is actually being produced
    - Verify simulation is running correctly

**Keywords Not Recognized**
    - Ensure ``Library RenodeKeywords`` is in the Settings section
    - Check that Renode integration is properly installed
    - Verify keyword spelling and syntax

See Also
********

- `Robot Framework Documentation <https://robotframework.org/>`_
- `Renode Documentation <https://renode.readthedocs.io/>`_
- :ref:`Test Configuration <test_config_args>`
- :ref:`Pytest Harness <twister_harness_pytest>` for Python-based testing
