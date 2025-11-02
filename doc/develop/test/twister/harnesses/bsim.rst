.. _twister_bsim_harness:

Bsim Harness
############

Overview
********

The ``bsim`` harness is a specialized harness designed for testing with the BabbleSim
(Bluetooth Simulator) framework. It provides a streamlined way to prepare Zephyr
applications for execution in BabbleSim's simulation environment.

Unlike most other harnesses, the Bsim harness has a very specific and limited scope:
it facilitates copying the built Zephyr executable to BabbleSim's binary directory,
making it available for BabbleSim test scenarios to execute.

When to Use Bsim Harness
=========================

Use the Bsim harness when:

* Testing Bluetooth functionality with BabbleSim
* Preparing applications for BabbleSim-based test scenarios
* You need to make test binaries available to BabbleSim's test infrastructure
* Working with multi-device Bluetooth simulation scenarios

The Bsim harness is specifically designed for:

* Bluetooth protocol testing
* Multi-device interaction simulation
* Bluetooth stack validation
* Complex Bluetooth scenarios that require precise timing and control

For more information about BabbleSim testing in Zephyr, see :ref:`bsim`.

How It Works
============

The Bsim harness performs a simple but essential operation:

1. **Locates the built executable**: Finds ``zephyr.exe`` in the build directory
   after Twister builds the test application

2. **Determines target name**: Creates a standardized filename based on platform
   and test information

3. **Copies to BabbleSim**: Places the executable in BabbleSim's bin directory
   (``${BSIM_OUT_PATH}/bin``) where BabbleSim test scripts can find and execute it

This preparation step allows BabbleSim's sophisticated test infrastructure to run
the Zephyr application within its simulation environment, enabling complex multi-device
scenarios and precise Bluetooth protocol testing.

.. note::
   The Bsim harness is implemented in a limited way compared to other harnesses.
   It does not execute tests or validate output - it only prepares the executable
   for BabbleSim to use. Actual test execution and validation happen through
   BabbleSim's test framework.

Configuration Options
*********************

The Bsim harness is configured in the test's YAML file. Configuration is minimal
since the harness has a focused purpose.

Basic Configuration
===================

To use the Bsim harness, simply specify it in your test configuration:

.. code-block:: yaml

    tests:
      bluetooth.test.example:
        harness: bsim
        platform_allow: native_sim
        tags: bluetooth

This minimal configuration is sufficient for most BabbleSim tests.

bsim_exe_name: <string> (optional)
===================================

By default, the executable filename when copying to BabbleSim's bin directory follows
this pattern:

.. code-block:: none

    bs_<platform_name>_<test_path>_<test_scenario_name>

Where:

* ``<platform_name>``: Target platform (e.g., ``native_sim``)
* ``<test_path>``: Path to test directory with dots and slashes replaced by underscores
* ``<test_scenario_name>``: Test scenario identifier

For example, a test at ``tests/bluetooth/host/gatt/gatt.read`` on ``native_sim`` becomes:

.. code-block:: none

    bs_native_sim_tests_bluetooth_host_gatt_gatt_read

Custom Executable Name
======================

If you need a specific executable name (e.g., for compatibility with existing BabbleSim
test scripts), use ``bsim_exe_name``:

.. code-block:: yaml

    tests:
      bluetooth.custom.test:
        harness: bsim
        harness_config:
          bsim_exe_name: my_test

This produces the executable name:

.. code-block:: none

    bs_<platform_name>_my_test

Example: For platform ``native_sim``, the result would be ``bs_native_sim_my_test``.

.. tip::
   Use ``bsim_exe_name`` when integrating with existing BabbleSim test suites that
   expect specific executable names.

Example Configurations
**********************

Basic BabbleSim Test
====================

Minimal configuration for a BabbleSim Bluetooth test:

.. code-block:: yaml

    tests:
      bluetooth.bsim.basic:
        harness: bsim
        platform_allow: native_sim
        tags: bluetooth bsim

Custom Executable Name
======================

Using a specific name for BabbleSim test script compatibility:

.. code-block:: yaml

    tests:
      bluetooth.bsim.legacy:
        harness: bsim
        platform_allow: native_sim
        harness_config:
          bsim_exe_name: legacy_gatt_test
        tags: bluetooth

Multi-Device Scenario
=====================

Setting up multiple related tests for a multi-device BabbleSim scenario:

.. code-block:: yaml

    tests:
      bluetooth.bsim.central:
        harness: bsim
        platform_allow: native_sim
        harness_config:
          bsim_exe_name: central_device
        tags: bluetooth role_central

      bluetooth.bsim.peripheral:
        harness: bsim
        platform_allow: native_sim
        harness_config:
          bsim_exe_name: peripheral_device
        tags: bluetooth role_peripheral

BabbleSim Environment Setup
****************************

Requirements
============

To use the Bsim harness, you need:

1. **BabbleSim installed**: The BabbleSim simulator must be installed and configured

2. **BSIM_OUT_PATH set**: Environment variable pointing to BabbleSim's output directory

3. **Native simulation platform**: Typically ``native_sim`` or similar platform

4. **Bluetooth configuration**: Application configured with Bluetooth support

Verifying Setup
===============

Before running BabbleSim tests, verify your environment:

.. code-block:: bash

    # Check BSIM_OUT_PATH is set
    echo $BSIM_OUT_PATH

    # Verify bin directory exists
    ls $BSIM_OUT_PATH/bin

    # Check BabbleSim is available
    which bs_device

Running BabbleSim Tests
***********************

Building with Twister
=====================

Use Twister to build the test application with the Bsim harness:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         $ ./scripts/twister -p native_sim -T tests/bluetooth/bsim

   .. group-tab:: Windows

      .. note::

         BabbleSim is primarily supported on Linux. Windows support may be limited.

This builds the application and copies the executable to BabbleSim's bin directory.

Running BabbleSim Test Scripts
===============================

After Twister builds and prepares the executables, run BabbleSim test scripts:

.. code-block:: bash

    # Navigate to BabbleSim test directory
    cd $BSIM_OUT_PATH/../components/ext_2G4_phy_v1/test_scripts

    # Run a BabbleSim test script
    ./run_test.sh

The BabbleSim test script will locate and execute the prepared executable(s) from
the bin directory.

Best Practices
**************

1. **Use consistent naming**: Stick with default naming unless you have a specific
   reason to customize. This makes test organization clearer.

2. **Set platform correctly**: Always use ``platform_allow: native_sim`` or the
   appropriate native simulation platform for BabbleSim.

3. **Tag appropriately**: Use tags like ``bluetooth`` and ``bsim`` to categorize
   and filter BabbleSim tests.

4. **Document multi-device scenarios**: When tests are part of multi-device scenarios,
   document which tests work together and their roles.

5. **Verify BSIM_OUT_PATH**: Always ensure the environment is properly configured
   before running BabbleSim tests.

6. **Clean up old binaries**: Periodically clean BabbleSim's bin directory to avoid
   confusion from stale executables.

7. **Version control test scripts**: Keep BabbleSim test scripts under version control
   alongside the test applications they execute.

Integration with BabbleSim Test Infrastructure
***********************************************

Multi-Device Scenarios
======================

BabbleSim excels at multi-device scenarios. A typical workflow:

1. **Define device roles**: Create separate test scenarios for each device role
   (central, peripheral, observer, etc.)

2. **Build with Twister**: Use Twister to build all device variants with appropriate
   ``bsim_exe_name`` values

3. **Create test script**: Write a BabbleSim test script that launches multiple
   instances and orchestrates their interaction

4. **Execute scenario**: Run the test script to execute the complete multi-device test

Example test script structure:

.. code-block:: bash

    #!/bin/bash
    # Multi-device BabbleSim test
    
    cd ${BSIM_OUT_PATH}/bin

    # Start central device
    ./bs_native_sim_central_device -s=test_sim -d=0 &
    
    # Start peripheral device
    ./bs_native_sim_peripheral_device -s=test_sim -d=1 &
    
    # Start PHY simulation
    ./bs_2G4_phy_v1 -s=test_sim -D=2 -sim_length=10e6 &
    
    wait

Timing and Synchronization
===========================

BabbleSim provides precise timing control, essential for Bluetooth protocol testing:

* Deterministic execution
* Synchronized device simulation
* Reproducible timing scenarios
* Precise control over radio conditions

Limitations
***********

Be aware of these limitations:

1. **Limited harness functionality**: Only copies executables; doesn't run tests
   or validate results

2. **Platform-specific**: Only works with native simulation platforms, not physical
   hardware

3. **Requires BabbleSim**: Must have BabbleSim installed and configured

4. **No automatic validation**: Test validation must be performed by BabbleSim test
   scripts, not by Twister

5. **Environment dependency**: Requires ``BSIM_OUT_PATH`` to be properly set

6. **Manual test execution**: After Twister builds, you must manually run BabbleSim
   test scripts (or script this separately)

Troubleshooting
***************

**Issue**: Executable not found in BabbleSim bin directory
    **Solution**: Verify ``BSIM_OUT_PATH`` is set correctly. Check Twister build
    completed successfully. Ensure harness is specified as ``bsim``.

**Issue**: BabbleSim script can't find executable
    **Solution**: Verify the executable name matches what the script expects. Use
    ``bsim_exe_name`` if needed to match script expectations.

**Issue**: BSIM_OUT_PATH not set error
    **Solution**: Set the environment variable to point to BabbleSim's output directory
    before running Twister.

**Issue**: Platform not supported
    **Solution**: Ensure you're using ``native_sim`` or another supported native
    simulation platform. BabbleSim doesn't work with physical hardware platforms.

**Issue**: Test builds but doesn't execute
    **Solution**: Remember the Bsim harness only prepares executables. You must
    separately run BabbleSim test scripts to execute the tests.

Additional Resources
********************

* :ref:`BabbleSim Testing Guide <bsim>`
* `BabbleSim Documentation <https://babblesim.github.io/>`_
* `BabbleSim GitHub Repository <https://github.com/BabbleSim>`_
* :ref:`Bluetooth Testing in Zephyr <bluetooth_testing>`
