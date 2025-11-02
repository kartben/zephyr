.. _twister_bsim_harness:

Bsim Harness
############

The ``bsim`` harness provides integration between Twister and BabbleSim (Bluetooth Simulator),
enabling automated testing of Bluetooth applications in a simulated environment. This harness
facilitates the deployment of Zephyr test executables to BabbleSim's binary directory,
allowing BabbleSim test scripts to execute them.

Overview
********

BabbleSim is a physical layer simulator for Bluetooth Low Energy and other 2.4GHz radios.
It allows testing Bluetooth protocol stacks and applications without physical hardware by
simulating the radio environment and multiple devices.

The bsim harness bridges Twister's build infrastructure with BabbleSim's execution
environment. While the harness itself has limited functionality, it plays a crucial role
in the BabbleSim testing workflow by ensuring test executables are properly deployed.

How It Works
************

The bsim harness performs a simple but essential task:

1. After Twister builds the test application, generating ``zephyr.exe``
2. The harness copies the executable to BabbleSim's binary directory (``${BSIM_OUT_PATH}/bin``)
3. The executable is renamed according to a standard naming convention
4. BabbleSim test scripts can then directly execute the deployed binary

This allows BabbleSim's test infrastructure to find and run Zephyr test applications without
manual deployment steps.

Executable Naming
*****************

By default, the executable is renamed following this pattern:

.. code-block:: none

   bs_<platform_name>_<test_path>_<test_scenario_name>

Where:

- Dots (``.``) are replaced with underscores (``_``)
- Slashes (``/``) are replaced with underscores (``_``)
- ``<platform_name>`` is the target platform (e.g., ``nrf52_bsim``)
- ``<test_path>`` is the test directory path
- ``<test_scenario_name>`` is the test scenario identifier

For example, a test at ``tests/bluetooth/host`` with scenario ``bluetooth.host.basic``
on ``nrf52_bsim`` becomes:

.. code-block:: none

   bs_nrf52_bsim_tests_bluetooth_host_bluetooth_host_basic

Configuration
*************

The bsim harness requires minimal configuration:

.. code-block:: yaml

   tests:
     bluetooth.simulation.test:
       harness: bsim
       platform_allow: nrf52_bsim

Custom Executable Name
======================

You can override the default naming with ``bsim_exe_name``:

bsim_exe_name: <string>
    Custom name for the executable. The final name will be:
    ``bs_<platform_name>_<bsim_exe_name>``

Example:

.. code-block:: yaml

   tests:
     bluetooth.custom:
       harness: bsim
       harness_config:
         bsim_exe_name: my_custom_test
       platform_allow: nrf52_bsim

This produces an executable named ``bs_nrf52_bsim_my_custom_test``.

Environment Setup
*****************

To use the bsim harness, your environment must have:

BSIM_OUT_PATH
    Environment variable pointing to BabbleSim's output directory. The harness copies
    executables to ``${BSIM_OUT_PATH}/bin``.

BabbleSim Installation
    BabbleSim must be installed and configured. See BabbleSim documentation for
    installation instructions.

Platform Support
    The test must target a BabbleSim-compatible platform (e.g., ``nrf52_bsim``,
    ``nrf5340bsim/nrf5340/cpunet``).

Integration with BabbleSim Tests
*********************************

The bsim harness is typically used in combination with BabbleSim's test scripts:

1. **Twister builds and deploys**: Twister compiles the test and the bsim harness
   copies it to BabbleSim's binary directory.

2. **BabbleSim executes**: Separate BabbleSim test scripts (usually shell scripts)
   launch the deployed executables in simulated scenarios.

3. **Results collected**: BabbleSim reports results, which can be integrated with
   Twister's reporting.

Example workflow:

.. code-block:: yaml

   # testcase.yaml
   tests:
     bluetooth.bsim.central:
       harness: bsim
       platform_allow: nrf52_bsim

.. code-block:: bash

   # Build with Twister (deploys executable)
   ./scripts/twister -p nrf52_bsim -T tests/bluetooth/

   # BabbleSim test script uses deployed executable
   # (typically run separately or via additional automation)
   ${BSIM_OUT_PATH}/bin/bs_nrf52_bsim_tests_bluetooth_...

Use Cases
*********

The bsim harness is used for:

**Bluetooth Protocol Testing**
    Test Bluetooth host stack, controllers, and profiles in simulation.

**Multi-Device Scenarios**
    Simulate multiple Bluetooth devices interacting with each other.

**RF Interference Testing**
    Test behavior under various radio conditions and interference.

**Automated CI Testing**
    Run Bluetooth tests in CI without physical hardware.

**Development and Debugging**
    Develop and debug Bluetooth features in a controlled, reproducible environment.

Limitations
***********

The bsim harness has intentional limitations:

**Limited Functionality**
    The harness only handles executable deployment. It does not:
    
    - Execute the test
    - Report test results
    - Configure BabbleSim simulation parameters
    - Manage multiple device instances

**No Test Orchestration**
    Complex multi-device scenarios require external test scripts.

**Build Only**
    Tests using bsim harness are effectively build-only from Twister's perspective.
    Actual test execution and validation happen through BabbleSim's infrastructure.

**Platform Specific**
    Only works with BabbleSim-compatible platforms.

Best Practices
**************

1. **Set platform_allow**: Always restrict bsim harness tests to BabbleSim platforms.

2. **Use meaningful names**: If using ``bsim_exe_name``, choose descriptive names that
   make the executable's purpose clear.

3. **Document dependencies**: Note in test documentation that BabbleSim must be installed
   and configured.

4. **Coordinate with scripts**: Ensure test executable names match what BabbleSim test
   scripts expect.

5. **Check environment**: Verify ``BSIM_OUT_PATH`` is set correctly before running tests.

Example Configuration
*********************

Complete example for a Bluetooth test:

.. code-block:: yaml

   common:
     tags: bluetooth simulation
   tests:
     bluetooth.bsim.init.split_privacy.central:
       extra_args: CONF_FILE="prj_split_privacy.conf"
       harness: bsim
       platform_allow: nrf52_bsim

     bluetooth.bsim.init.split_privacy.peripheral:  
       extra_args: CONF_FILE="prj_split_privacy.conf"
       harness: bsim
       platform_allow: nrf52_bsim

     bluetooth.bsim.init.split_low_lat.central:
       extra_args: CONF_FILE="prj_split_low_lat.conf"
       harness: bsim
       harness_config:
         bsim_exe_name: central_low_lat
       platform_allow: nrf52_bsim

See Also
********

- :ref:`bsim` - Main BabbleSim documentation in Zephyr
- :ref:`twister_harnesses` - Overview of all available harnesses
- :ref:`twister_script` - Main Twister documentation
- BabbleSim documentation - For detailed BabbleSim usage
