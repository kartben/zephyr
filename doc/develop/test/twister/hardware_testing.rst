.. _twister_hardware_testing:

Hardware Testing with Twister
##############################

Twister provides comprehensive support for running tests on real hardware devices,
enabling validation of Zephyr applications on physical boards. This capability is
essential for verifying device drivers, hardware-specific functionality, and
real-world behavior that cannot be fully simulated.

Overview
********

While emulation and simulation (such as QEMU) are valuable for quick iteration and
CI testing, physical hardware testing is crucial for:

- Validating device drivers and hardware interfaces
- Testing real-world timing and performance
- Verifying power consumption characteristics
- Testing board-specific features
- Ensuring production readiness

Twister automates the entire hardware testing workflow, including building,
flashing, execution, and result collection, making it practical to run comprehensive
test suites on physical devices.

.. _single_device_testing:

Testing on a Single Device
***************************

For quick testing or when you have only one board available, Twister can be
configured to test on a single connected device.

Basic Single Device Testing
============================

Use the ``--device-testing`` flag along with device-specific options:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         scripts/twister --device-testing --device-serial /dev/ttyACM0 \
         --device-serial-baud 115200 -p frdm_k64f -T tests/kernel

   .. group-tab:: Windows

      .. code-block:: bat

         python .\scripts\twister --device-testing --device-serial COM1 \
         --device-serial-baud 115200 -p frdm_k64f -T tests/kernel

Command-Line Options
=====================

--device-testing
    Enables device testing mode (as opposed to emulation/simulation)

--device-serial <port>
    Serial port device path (e.g., ``/dev/ttyACM0`` on Linux, ``COM1`` on Windows).
    Must be accessible by the user running Twister.

--device-serial-baud <baud_rate> (optional, default 115200)
    Serial port baud rate. Only needed if your device doesn't use 115200.

-p, --platform <platform_name>
    Specifies which platform/board to test on. Required for single device testing.

Serial PTY Support
==================

For devices without a physical serial port, use a script-based PTY (pseudo-terminal):

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         scripts/twister --device-testing --device-serial-pty "script.py" \
         -p intel_adsp/cavs25 -T tests/kernel

   .. group-tab:: Windows

      .. note::

         PTY support is not available on Windows

The script is user-defined and handles message delivery for test status determination.

Flash Timeout Options
=====================

--device-flash-timeout <seconds>
    Sets explicit timeout for the flash operation. Useful when flashing takes
    significantly longer than the default timeout.

--device-flash-with-test
    Indicates that the flash operation also executes the test scenario. The flash
    timeout will be increased by the test scenario timeout.

Example:

.. code-block:: bash

   scripts/twister --device-testing --device-serial /dev/ttyACM0 \
   -p board_name --device-flash-timeout 60 --device-flash-with-test \
   -T tests/subsys/logging

.. _multiple_device_testing:

Testing on Multiple Devices
****************************

For comprehensive testing or CI environments, Twister supports testing on multiple
devices simultaneously using a hardware map file.

Hardware Map Overview
=====================

A hardware map is a YAML file that describes all connected devices and their
properties. Twister uses this to:

- Automatically select appropriate boards for tests
- Manage parallel test execution across devices
- Track device availability and status
- Match test requirements with device capabilities

Generating a Hardware Map
==========================

Twister can automatically detect connected devices and generate an initial hardware map:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         ./scripts/twister --generate-hardware-map map.yml

   .. group-tab:: Windows

      .. code-block:: bat

         python .\scripts\twister --generate-hardware-map map.yml

This generates a ``map.yml`` file with detected devices. Example output:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: yaml

         - connected: true
           id: OSHW000032254e4500128002ab98002784d1000097969900
           platform: unknown
           product: DAPLink CMSIS-DAP
           runner: pyocd
           serial: /dev/cu.usbmodem146114202
         - connected: true
           id: 000683759358
           platform: unknown
           product: J-Link
           runner: unknown
           serial: /dev/cu.usbmodem0006837593581

   .. group-tab:: Windows

      .. code-block:: yaml

         - connected: true
           id: OSHW000032254e4500128002ab98002784d1000097969900
           platform: unknown
           product: unknown
           runner: unknown
           serial: COM1
         - connected: true
           id: 000683759358
           platform: unknown
           product: unknown
           runner: unknown
           serial: COM2

Configuring the Hardware Map
=============================

The generated map requires manual editing to fill in ``unknown`` values. Update
the platform names, products, and runners to match your actual hardware.

Example configured hardware map:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: yaml

         - connected: true
           id: OSHW000032254e4500128002ab98002784d1000097969900
           platform: reel_board
           product: DAPLink CMSIS-DAP
           runner: pyocd
           serial: /dev/cu.usbmodem146114202
           baud: 9600
         - connected: true
           id: 000683759358
           platform: nrf52840dk/nrf52840
           product: J-Link
           runner: nrfjprog
           serial: /dev/cu.usbmodem0006837593581
           baud: 9600

   .. group-tab:: Windows

      .. code-block:: yaml

         - connected: true
           id: OSHW000032254e4500128002ab98002784d1000097969900
           platform: reel_board
           product: DAPLink CMSIS-DAP
           runner: pyocd
           serial: COM1
           baud: 9600
         - connected: true
           id: 000683759358
           platform: nrf52840dk/nrf52840
           product: J-Link
           runner: nrfjprog
           serial: COM2
           baud: 9600

The ``baud`` entry is only needed if the device doesn't use 115200.

Running Tests with Hardware Map
================================

Once the hardware map is configured, run tests by referencing it:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         ./scripts/twister --device-testing --hardware-map map.yml -T samples/hello_world/

   .. group-tab:: Windows

      .. code-block:: bat

         python .\scripts\twister --device-testing --hardware-map map.yml -T samples\hello_world

Twister will:

1. Build tests for platforms defined in the hardware map
2. Flash each test to the appropriate device
3. Execute tests and collect results
4. Generate comprehensive reports

Updating the Hardware Map
==========================

If you run ``--generate-hardware-map`` on an existing map file, Twister will:

- Add newly detected devices
- Update status and serial ports of existing devices
- Preserve manually configured values

This allows maintaining a master hardware map that stays current across runs.

Hardware Map Configuration Options
***********************************

The hardware map supports various configuration options to customize device behavior.

Required Fields
===============

connected: <true|false>
    Indicates whether the device is currently connected and available

id: <string>
    Unique identifier for the device (usually detected automatically)

platform: <string or list>
    Platform name(s) this device supports

product: <string>
    Product name (usually detected automatically)

runner: <string>
    West runner to use for flashing (e.g., ``pyocd``, ``jlink``, ``nrfjprog``, ``openocd``)

serial: <string>
    Serial port device path

Optional Fields
===============

baud: <integer> (default 115200)
    Serial port baud rate

fixtures: <list of strings>
    Hardware fixtures available on this device. See :ref:`hardware_fixtures`.

flash-timeout: <integer>
    Device-specific flash timeout (overrides ``--device-flash-timeout``)

flash-with-test: <true|false>
    Device-specific flash-with-test flag (overrides ``--device-flash-with-test``)

serial_pty: <string>
    Path to PTY script for devices without physical serial ports

runner_params: <list of strings>
    Additional parameters to pass to the west runner

probe_id: <string>
    Override the detected ID for flashing (useful with external probes)

notes: <string>
    Free-form notes about the device or configuration

Advanced Configuration Examples
================================

Serial PTY Configuration
-------------------------

.. code-block:: yaml

   - connected: true
     id: None
     platform: intel_adsp/cavs25
     product: None
     runner: intel_adsp
     serial_pty: path/to/script.py
     runner_params:
       - --remote-host=remote_host_ip_addr
       - --key=/path/to/key.pem

.. note::

   PTY devices cannot be auto-detected. You must manually configure them in the
   hardware map, as the serial port is allocated at runtime.

Custom Flash Command
--------------------

If west doesn't support your flash method, use a custom flash command:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         twister -p npcx9m6f_evb --device-testing --device-serial /dev/ttyACM0 \
         --flash-command './custom_flash_script.py,--flag,"complex, argument"'

   .. group-tab:: Windows

      .. code-block:: bash

         python .\scripts\twister -p npcx9m6f_evb --device-testing \
         --device-serial COM1 \
         --flash-command 'custom_flash_script.py,--flag,"complex, argument"'

The script receives ``--build-dir`` and ``--board-id`` arguments:

.. code-block:: bash

   ./custom_flash_script.py \
     --build-dir <build directory> \
     --board-id <board identification> \
     --flag "complex, argument"

Overriding Board Identifier
----------------------------

For external debug probes or misdetected IDs, use ``probe_id``:

.. code-block:: yaml

   - connected: false
     id: 0229000005d9ebc600000000000000000000000097969905
     platform: mimxrt1060_evk
     probe_id: 000609301751
     product: DAPLink CMSIS-DAP
     runner: jlink
     serial: null

Multiple Platform Variants
---------------------------

Test multiple variants on one physical board:

.. code-block:: yaml

   - connected: true
     id: '001234567890'
     platform:
       - nrf5340dk/nrf5340/cpuapp
       - nrf5340dk/nrf5340/cpuapp/ns
     product: J-Link
     runner: nrfjprog
     serial: /dev/ttyACM1

Adding Notes
------------

Document special configurations or issues:

.. code-block:: yaml

   - connected: false
     fixtures:
       - gpio_loopback
     id: 000683290670
     notes: An nrf5340dk/nrf5340 is detected as an nrf52840dk/nrf52840 with no serial
       port, and three serial ports with an unknown platform. The board id of the serial
       ports is not the same as the board id of the development kit. If you regenerate
       this file you will need to update serial to reference the third port, and platform
       to nrf5340dk/nrf5340/cpuapp or another supported board target.
     platform: nrf52840dk/nrf52840
     product: J-Link
     runner: jlink
     serial: null

.. _hardware_fixtures:

Hardware Fixtures
*****************

Some tests require specific hardware setup or wiring configurations called fixtures.
Fixtures enable Twister to match tests with appropriately configured hardware.

Defining Fixtures in Hardware Map
==================================

Specify fixtures as a list in the hardware map:

.. code-block:: yaml

   - connected: true
     fixtures:
       - gpio_loopback
       - i2c_hts221
     id: 0240000026334e450015400f5e0e000b4eb1000097969900
     platform: frdm_k64f
     product: DAPLink CMSIS-DAP
     runner: pyocd
     serial: /dev/ttyACM9

Requesting Fixtures in Tests
=============================

Tests specify required fixtures in their configuration:

.. code-block:: yaml

   tests:
     drivers.gpio.loopback:
       harness: console
       harness_config:
         type: multi_line
         fixture: gpio_loopback
         regex:
           - "GPIO loopback test passed"

Fixture Matching
================

When using ``--device-testing`` with a hardware map, Twister:

1. Identifies test fixture requirements
2. Matches tests to devices providing those fixtures
3. Only executes tests on appropriately equipped hardware

Command-Line Fixtures
=====================

Provide fixtures via command line to apply to all devices:

.. code-block:: bash

   ./scripts/twister --device-testing --hardware-map map.yml \
   --fixture gpio_loopback --fixture i2c_sensor \
   -T tests/drivers/

Multiple ``--fixture`` options can be used. All specified fixtures are assigned
to all boards in the current run.

Fixture Configuration Strings
==============================

Some fixtures support configuration strings appended after a colon:

.. code-block:: yaml

   fixtures:
     - sensor_board:config_variant_a
     - power_supply:5v

Only the fixture name (before the colon) is used for matching. The configuration
string is available to test harnesses for customization.

.. figure:: figures/fixtures.svg
   :figclass: align-center
   :alt: Diagram showing fixture matching between tests and hardware

   Twister matches test fixture requirements with hardware capabilities

Quarantine
**********

The quarantine feature allows temporarily excluding problematic tests or platforms
from test runs, which is valuable for large test suites where one failure can
impact other tests.

Using Quarantine
================

Specify quarantine files with ``--quarantine-list``:

.. code-block:: bash

   ./scripts/twister --quarantine-list quarantine.yml -T tests/

Multiple quarantine files can be specified:

.. code-block:: bash

   ./scripts/twister --quarantine-list q1.yml --quarantine-list q2.yml -T tests/

Quarantine Verification Mode
=============================

Use ``--quarantine-verify`` to run *only* tests on the quarantine list:

.. code-block:: bash

   ./scripts/twister --quarantine-list quarantine.yml --quarantine-verify -T tests/

This helps verify whether quarantined tests are still failing or can be restored.

Quarantine File Format
======================

Quarantine files are YAML sequences of dictionaries. Each dictionary must have
at least one of:

- ``scenarios``: List of test scenarios to quarantine
- ``platforms``: List of platforms to exclude
- ``architectures``: List of architectures to exclude
- ``simulations``: List of simulators to exclude

An optional ``comment`` field documents the reason (e.g., link to issue).

Examples
--------

Quarantine specific scenario:

.. code-block:: yaml

   - scenarios:
       - sample.basic.helloworld
     comment: "Link to the issue: https://github.com/zephyrproject-rtos/zephyr/pull/33287"

Quarantine multiple scenarios on specific platforms:

.. code-block:: yaml

   - scenarios:
       - kernel.common
       - kernel.common.(misra|tls)
       - kernel.common.nano64
     platforms:
       - .*_cortex_.*
       - native_sim

Quarantine all tests on a platform:

.. code-block:: yaml

   - platforms:
       - qemu_x86
     comment: "Platform temporarily unavailable"

Quarantine by architecture:

.. code-block:: yaml

   - architectures:
       - riscv
     comment: "Known RISC-V toolchain issue"

Quarantine by simulation:

.. code-block:: yaml

   - simulations:
       - armfvp
     comment: "ARMfvp simulator issues"

Regular Expression Support
===========================

Quarantine supports regular expressions for matching:

.. code-block:: yaml

   - scenarios:
       - kernel.*      # All kernel tests
       - bluetooth\..*  # All Bluetooth tests
       - .*\.basic     # All tests ending with .basic

This is powerful for quarantining entire subsystems or classes of tests.

Supported Runners
*****************

Twister's hardware map feature currently supports these west flash runners:

- ``pyocd``
- ``nrfjprog``  
- ``jlink``
- ``openocd``
- ``dediprog``
- ``intel_adsp`` (with PTY)

.. note::

   Support for additional runners is ongoing. Boards requiring other runners
   may need custom flash commands or manual flashing procedures.

Best Practices
**************

Hardware Map Management
=======================

1. **Version control**: Store your hardware map in version control to track
   changes and share configurations.

2. **Document boards**: Use the ``notes`` field to document special configurations,
   known issues, or setup requirements.

3. **Update regularly**: Regenerate periodically to detect new devices and update
   serial ports.

4. **Multiple maps**: Maintain different maps for different test environments
   (CI, development, validation).

5. **Test incrementally**: Start with one or two boards, verify they work, then
   expand to more platforms.

Test Organization
=================

1. **Use fixtures wisely**: Define fixtures for hardware that requires special
   setup or wiring.

2. **Mark build-only tests**: For hardware-specific tests that can't run
   everywhere, use ``build_only`` or ``platform_allow``.

3. **Set appropriate timeouts**: Hardware tests may take longer than simulated
   tests. Adjust timeouts accordingly.

4. **Handle flaky tests**: Use quarantine for intermittent failures while
   investigating root causes.

CI Integration
==============

1. **Parallel execution**: Leverage multiple devices for faster CI runs.

2. **Board rotation**: Rotate which boards are tested to maximize coverage
   over time.

3. **Quarantine management**: Regularly review and update quarantine lists.

4. **Result archiving**: Preserve test logs and reports for debugging and
   compliance.

Troubleshooting
***************

Device Not Detected
===================

- Verify USB connection and cable quality
- Check device appears in system (``lsusb`` on Linux, Device Manager on Windows)
- Ensure appropriate drivers are installed
- Check user has permission to access the device

Flash Failures
==============

- Verify correct runner is specified
- Check runner software is installed and in PATH
- Ensure board is not locked or protected
- Try increasing ``flash-timeout``
- Verify probe firmware is up to date

Serial Communication Issues
===========================

- Confirm correct serial port and baud rate
- Check no other process is using the serial port
- Verify cables and connections
- Test serial communication manually (e.g., with ``minicom`` or ``screen``)

Inconsistent Results
====================

- Check for intermittent hardware issues
- Ensure consistent power supply
- Look for timing-dependent test failures
- Consider environmental factors (temperature, interference)

See Also
********

- :ref:`twister_harnesses` - Information on test harnesses
- :ref:`twister_script` - Main Twister documentation
- :ref:`west-build-flash-debug` - West flash runner documentation
- :ref:`board_porting` - Board porting guide including hardware map setup
