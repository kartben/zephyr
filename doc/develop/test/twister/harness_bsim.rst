.. _twister_harness_bsim:

Bsim Harness
############

.. _twister_bsim_harness:

Overview
********

The Bsim (BabbleSim) harness is a specialized utility for integrating Zephyr tests with
the BabbleSim simulation framework. BabbleSim is a physical layer simulator designed for
wireless protocol testing, particularly for Bluetooth Low Energy and other radio protocols.

Unlike most harnesses that monitor test execution and determine pass/fail status, the
Bsim harness has a more limited scope: it facilitates running tests in the BabbleSim
environment by managing test executables. Specifically, it copies the built test
executable (``zephyr.exe``) from Twister's build directory to BabbleSim's binary
directory, making it available for BabbleSim's own test orchestration.

This harness is essential for:

- **BLE protocol testing** in simulated environments
- **Multi-device scenarios** where multiple simulated devices interact
- **RF layer testing** without requiring physical hardware
- **Wireless protocol development** and validation
- **CI/CD integration** of BabbleSim-based tests

The Bsim harness serves as a bridge between Twister's build system and BabbleSim's
test execution environment.

How It Works
************

The Bsim harness implements a simple but important workflow:

1. **Test Build**: Twister builds the test application targeting a BabbleSim platform
   (e.g., ``nrf52_bsim``)

2. **Executable Location**: The build produces a ``zephyr.exe`` file in the build directory

3. **File Copy**: The Bsim harness copies this executable to BabbleSim's binary directory
   (``${BSIM_OUT_PATH}/bin``)

4. **Naming**: The executable is renamed following BabbleSim conventions:
   ``bs_<platform>_<test_path>_<test_scenario>``

5. **Availability**: The renamed executable is now available for BabbleSim test scripts
   to launch and orchestrate

After this, actual test execution and result determination is handled by BabbleSim's
own test framework, not by Twister.

Prerequisites
*************

To use the Bsim harness, you need:

1. **BabbleSim Installation**: BabbleSim simulator must be installed and configured

2. **Environment Variable**: ``BSIM_OUT_PATH`` must be set to BabbleSim's output directory

3. **BabbleSim Platform**: Test must target a BabbleSim-compatible platform (e.g.,
   ``nrf52_bsim``, ``nrf5340bsim_nrf5340_cpuapp``)

4. **Test Application**: Application must be compatible with BabbleSim execution

Configuration
*************

The Bsim harness is specified using the standard harness syntax:

.. code-block:: yaml

    tests:
      bluetooth.bsim.test:
        harness: bsim
        platform_allow: nrf52_bsim

Optional Configuration
======================

**bsim_exe_name: <string>** (optional)
    Customize the executable filename in BabbleSim's bin directory.
    
    Default naming: ``bs_<platform>_<test_path>_<test_scenario>``
    
    With custom name: ``bs_<platform>_<bsim_exe_name>``

Basic Example
*************

Simple BLE Test
===============

**testcase.yaml**:

.. code-block:: yaml

    tests:
      bluetooth.basic.bsim:
        harness: bsim
        platform_allow: nrf52_bsim
        tags: bluetooth

This produces an executable named something like:
``bs_nrf52_bsim_tests_bluetooth_basic_bsim``

With Custom Executable Name
============================

**testcase.yaml**:

.. code-block:: yaml

    tests:
      bluetooth.advertiser:
        harness: bsim
        platform_allow: nrf52_bsim
        harness_config:
          bsim_exe_name: ble_advertiser

This produces: ``bs_nrf52_bsim_ble_advertiser``

The custom name makes it easier to reference the executable in BabbleSim test scripts.

Multi-Device Scenario
=====================

BabbleSim excels at multi-device testing. You typically build multiple test applications:

**Central device - testcase.yaml**:

.. code-block:: yaml

    tests:
      bluetooth.central:
        harness: bsim
        platform_allow: nrf52_bsim
        harness_config:
          bsim_exe_name: ble_central

**Peripheral device - testcase.yaml**:

.. code-block:: yaml

    tests:
      bluetooth.peripheral:
        harness: bsim
        platform_allow: nrf52_bsim
        harness_config:
          bsim_exe_name: ble_peripheral

After building both tests with Twister, BabbleSim can orchestrate a scenario where
both devices interact.

Understanding Executable Naming
********************************

The default naming convention uses underscores to replace dots and slashes from the
test path and scenario name:

.. code-block:: none

    Test path: tests/bluetooth/host/gatt/notify
    Scenario:  bluetooth.host.gatt.notify.test
    Platform:  nrf52_bsim
    
    Default name:
    bs_nrf52_bsim_tests_bluetooth_host_gatt_notify_bluetooth_host_gatt_notify_test

This can become quite long, which is why ``bsim_exe_name`` is useful for creating
shorter, more memorable names.

With ``bsim_exe_name: gatt_notify``:

.. code-block:: none

    Result: bs_nrf52_bsim_gatt_notify

Integration with BabbleSim Test Scripts
****************************************

After Twister copies the executables, you typically use BabbleSim's own test
orchestration to run the actual tests:

Example BabbleSim test script:

.. code-block:: bash

    #!/usr/bin/env bash
    # BabbleSim test script
    
    SIMULATION_ID="gatt_test"
    VERBOSITY_LEVEL=2
    
    # Start the physical layer simulator
    ${BSIM_OUT_PATH}/bin/bs_2G4_phy_v1 \
        -s=$SIMULATION_ID -D=2 -v=${VERBOSITY_LEVEL} &
    
    # Start peripheral device
    ${BSIM_OUT_PATH}/bin/bs_nrf52_bsim_ble_peripheral \
        -s=$SIMULATION_ID -d=0 -v=${VERBOSITY_LEVEL} &
    
    # Start central device
    ${BSIM_OUT_PATH}/bin/bs_nrf52_bsim_ble_central \
        -s=$SIMULATION_ID -d=1 -v=${VERBOSITY_LEVEL} &
    
    wait

BabbleSim handles the actual test execution, device coordination, and result reporting.

Typical Workflow
****************

1. **Write Test Applications**: Create Zephyr applications for each simulated device

2. **Configure for Bsim**: Use ``harness: bsim`` and appropriate platform in testcase.yaml

3. **Build with Twister**: Run Twister to build test applications:

   .. code-block:: bash

       ./scripts/twister -p nrf52_bsim -T tests/bluetooth/

4. **Executables Copied**: Bsim harness copies executables to ``${BSIM_OUT_PATH}/bin``

5. **Run BabbleSim Tests**: Execute BabbleSim orchestration scripts:

   .. code-block:: bash

       cd tests/bluetooth/host/
       ./run_bsim_tests.sh

6. **Analyze Results**: Review BabbleSim's test output and logs

Best Practices
**************

1. **Use Custom Names**: For complex test suites, use ``bsim_exe_name`` for clarity:

   .. code-block:: yaml

       harness_config:
         bsim_exe_name: short_descriptive_name

2. **Document Dependencies**: Clearly document which executables work together in
   multi-device scenarios

3. **Version Control**: Keep BabbleSim test scripts under version control alongside
   test applications

4. **Consistent Naming**: Use consistent naming patterns across related tests:

   .. code-block:: yaml

       # Good: clear relationship
       bsim_exe_name: gatt_server
       bsim_exe_name: gatt_client
       
       # Bad: unclear relationship
       bsim_exe_name: test1
       bsim_exe_name: test2

5. **Cleanup**: Remember that executables accumulate in ``${BSIM_OUT_PATH}/bin``;
   periodically clean up old test binaries

6. **Platform Restrictions**: Always use ``platform_allow`` to restrict tests to
   BabbleSim platforms:

   .. code-block:: yaml

       platform_allow: nrf52_bsim nrf5340bsim_nrf5340_cpuapp

Limitations
***********

The Bsim harness is intentionally limited in scope:

- **No Test Execution**: It doesn't run tests, only prepares executables
- **No Result Checking**: It doesn't determine pass/fail; BabbleSim scripts handle this
- **No Output Parsing**: It doesn't monitor or analyze test output
- **Build-Time Only**: It operates during the build phase, not execution phase

These limitations are by design - the harness focuses on build system integration,
leaving test orchestration and validation to BabbleSim's specialized tools.

BabbleSim Platforms
*******************

Common BabbleSim platforms in Zephyr:

- ``nrf52_bsim`` - nRF52 series simulation
- ``nrf5340bsim_nrf5340_cpuapp`` - nRF5340 application core
- ``nrf5340bsim_nrf5340_cpunet`` - nRF5340 network core

Check ``boards/`` directory for the complete list of BabbleSim-supported boards.

Troubleshooting
***************

Common Issues
=============

**BSIM_OUT_PATH Not Set**
    Ensure the environment variable is set:
    
    .. code-block:: bash
    
        export BSIM_OUT_PATH=/path/to/babblesim/bin

**Executable Not Found in BabbleSim**
    - Verify ``${BSIM_OUT_PATH}/bin`` exists
    - Check that Twister build completed successfully
    - Ensure file permissions allow copying
    - Look for error messages in Twister output

**Wrong Platform**
    Ensure you're targeting a BabbleSim platform:
    
    .. code-block:: bash
    
        # Correct
        ./scripts/twister -p nrf52_bsim
        
        # Wrong - real hardware
        ./scripts/twister -p nrf52840dk_nrf52840

**Name Conflicts**
    If using custom names, ensure they're unique across all tests that might
    run concurrently.

See Also
********

- `BabbleSim Documentation <https://babblesim.github.io/>`_
- :ref:`Bluetooth Testing <bluetooth_testing>`
- BabbleSim integration in ``tests/bluetooth/`` directory
- :ref:`Test Configuration <test_config_args>`
