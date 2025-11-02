.. _twister_power_harness:

Power Harness
#############

Overview
********

The ``power`` harness is a specialized testing tool designed to measure and validate
current consumption in embedded systems. It integrates with the pytest framework to
perform automated data collection and analysis using hardware power monitoring equipment.

This harness enables precise power profiling of your applications, allowing you to verify
that power consumption meets specifications across different operating modes and transitions.
It's particularly valuable for battery-powered devices where power efficiency is critical.

When to Use Power Harness
==========================

Use the power harness when:

* Measuring and validating current consumption of your application
* Testing power management features and low-power modes
* Verifying power consumption meets specified requirements
* Profiling power usage across different execution phases
* Testing battery life optimization
* Validating power state transitions
* Ensuring consistent power behavior across builds

The power harness is essential for:

* Battery-powered IoT devices
* Energy harvesting applications  
* Low-power wireless devices
* Applications with strict power budgets
* Devices that must meet power consumption specifications
* Optimizing power management strategies

How It Works
============

The power harness orchestrates automated power measurement and validation:

1. **Initialization**: Sets up a power monitoring device (e.g., ``stm_powershield``)
   through the ``PowerMonitor`` abstract interface

2. **Measurement**: Starts current measurement for a defined duration, collecting
   raw current waveform data from the hardware

3. **Segmentation**: Uses peak detection algorithms to identify power transitions
   and segment the data into distinct execution phases

4. **Analysis**: Computes RMS (Root Mean Square) current values for each identified
   phase using statistical utilities

5. **Validation**: Compares computed values against user-defined expected RMS values
   within specified tolerance ranges

6. **Reporting**: Reports results through Twister's standard reporting mechanisms,
   including detailed metrics and pass/fail status

This automated approach ensures consistent, repeatable power measurements across
development cycles and hardware configurations.

Hardware Requirements
*********************

To use the power harness, you need:

1. **Power monitoring hardware**: A device capable of measuring current with sufficient
   precision and sampling rate (e.g., STM PowerShield, Nordic Power Profiler Kit, Joulescope)

2. **Device under test (DUT)**: Your target hardware configured for external power
   monitoring

3. **Test fixture**: Physical connection between the power monitor and DUT, configured
   in the hardware map with the ``pm_probe`` fixture

4. **Host computer**: System running Twister with appropriate drivers for the power
   monitoring hardware

Supported Power Monitors
=========================

Currently supported power monitoring devices:

* **STM PowerShield**: Through the ``stm_powershield`` implementation

Additional power monitors can be integrated by implementing the ``PowerMonitor``
abstract interface.

Configuration Options
*********************

The power harness is configured in the test's YAML file under ``harness_config``.
All power-specific settings are under the ``power_measurements`` subsection.

Basic Configuration
===================

.. code-block:: yaml

    harness: power
    harness_config:
      fixture: pm_probe
      power_measurements:
        measurement_duration: 6
        num_of_transitions: 4
        expected_rms_values: [56.0, 4.0, 1.2, 0.26, 140]
        tolerance_percentage: 20

Required Parameters
===================

fixture: pm_probe
    Specifies that this test requires a power monitoring fixture. This must be present
    in the hardware map configuration for the test to run on a specific board.

measurement_duration: <float>
    Total time (in seconds) to record current data from the power monitor.

    The duration should be long enough to capture all expected power transitions and
    states, but not unnecessarily long to avoid slowing down test execution.

    Example: ``measurement_duration: 6`` for a 6-second measurement window.

num_of_transitions: <integer>
    Expected number of power state transitions in the DUT during test execution.

    This helps the peak detection algorithm identify execution phases. The number of
    transitions plus one equals the number of expected phases (since there's a phase
    between each transition).

    Example: ``num_of_transitions: 4`` expects 4 transitions, resulting in 5 phases.

expected_rms_values: <list of floats>
    Target RMS current values for each identified execution phase, specified in milliamps (mA).

    The list should contain one value for each expected phase (num_of_transitions + 1).
    Values represent the expected steady-state current during each phase.

    Example: ``expected_rms_values: [56.0, 4.0, 1.2, 0.26, 140]`` expects five phases
    with currents of approximately 56mA, 4mA, 1.2mA, 0.26mA, and 140mA respectively.

tolerance_percentage: <float>
    Allowed deviation percentage from the expected RMS values.

    This accounts for normal variations in power consumption due to environmental factors,
    measurement precision, and device tolerances.

    Example: ``tolerance_percentage: 20`` allows ±20% deviation from expected values.

    .. tip::
       Start with a larger tolerance (e.g., 20-30%) and tighten it as you gain confidence
       in your measurements and application consistency.

Signal Processing Parameters
=============================

These parameters control the peak detection and signal segmentation algorithms:

element_to_trim: <integer>
    Number of samples to discard at the start of measurement to eliminate initialization
    noise and transients.

    Power monitors may produce unstable readings at the start of measurement. Trimming
    these samples ensures analysis begins with clean data.

    Example: ``element_to_trim: 100`` discards the first 100 samples.

min_peak_distance: <integer>
    Minimum distance (in samples) between detected current peaks.

    This prevents detecting multiple peaks for a single transition due to noise or
    ripple. Higher values reduce false peak detection but may miss rapid transitions.

    Example: ``min_peak_distance: 40`` requires at least 40 samples between peaks.

min_peak_height: <float>
    Minimum current threshold (in amps) to qualify as a peak.

    Smaller current changes below this threshold are ignored. This filters out noise
    and minor fluctuations that don't represent actual power state transitions.

    Example: ``min_peak_height: 0.008`` (8mA minimum peak height).

    .. note::
       This value is in amps, not milliamps, to match the raw measurement units.

peak_padding: <integer>
    Number of samples to extend around each detected peak.

    This defines the region around each transition to include in the analysis, capturing
    the full transition behavior rather than just the peak instant.

    Example: ``peak_padding: 40`` includes 40 samples before and after each peak.

Complete Example
================

Here's a comprehensive power harness configuration:

.. code-block:: yaml

    tests:
      power.profile.sleep_modes:
        harness: power
        tags: power low_power
        timeout: 30
        harness_config:
          fixture: pm_probe
          power_measurements:
            # Measurement settings
            measurement_duration: 6
            num_of_transitions: 4

            # Signal processing
            element_to_trim: 100
            min_peak_distance: 40
            min_peak_height: 0.008
            peak_padding: 40

            # Validation thresholds
            expected_rms_values: [56.0, 4.0, 1.2, 0.26, 140]
            tolerance_percentage: 20

Example Configurations
**********************

Testing Deep Sleep States
=========================

Configuration for validating transitions through multiple sleep states:

.. code-block:: yaml

    tests:
      power.sleep.deep_states:
        harness: power
        platform_allow: nrf52840dk/nrf52840
        harness_config:
          fixture: pm_probe
          power_measurements:
            measurement_duration: 10
            num_of_transitions: 3
            expected_rms_values: [5.2, 0.8, 0.3, 45.0]  # active, sleep, deep_sleep, wake
            tolerance_percentage: 15
            element_to_trim: 150
            min_peak_distance: 50
            min_peak_height: 0.005
            peak_padding: 30

Testing Radio Power Modes
==========================

Configuration for wireless device power profiling:

.. code-block:: yaml

    tests:
      power.radio.tx_rx_cycles:
        harness: power
        depends_on: ble
        harness_config:
          fixture: pm_probe
          power_measurements:
            measurement_duration: 8
            num_of_transitions: 6
            expected_rms_values: [2.5, 12.3, 2.5, 8.7, 2.5, 12.3, 2.5]  
            # idle, tx, idle, rx, idle, tx, idle
            tolerance_percentage: 25  # Radio can vary more
            element_to_trim: 200
            min_peak_distance: 60
            min_peak_height: 0.010
            peak_padding: 50

Quick Active/Sleep Test
========================

Minimal configuration for basic sleep mode validation:

.. code-block:: yaml

    tests:
      power.basic.sleep:
        harness: power
        harness_config:
          fixture: pm_probe
          power_measurements:
            measurement_duration: 4
            num_of_transitions: 1
            expected_rms_values: [15.0, 0.5]  # active, sleep
            tolerance_percentage: 30

Writing Power Tests
*******************

Application Code Requirements
==============================

Your test application should be structured to create measurable power transitions:

.. code-block:: c

    #include <zephyr/kernel.h>
    #include <zephyr/pm/pm.h>

    void main(void)
    {
        /* Phase 1: Active state - initialization */
        k_sleep(K_SECONDS(1));

        /* Phase 2: Low power state */
        pm_state_force(0U, PM_STATE_SUSPEND_TO_IDLE);
        k_sleep(K_SECONDS(2));

        /* Phase 3: Even lower power state */
        pm_state_force(0U, PM_STATE_SUSPEND_TO_RAM);
        k_sleep(K_SECONDS(2));

        /* Phase 4: Wake up and process */
        pm_state_force_all_disable(0U);
        k_sleep(K_SECONDS(1));
    }

Best Practices for Power Testing
=================================

1. **Create clear transitions**: Ensure distinct power states with sufficient duration
   for accurate measurement. Avoid rapid state changes that may blur transitions.

2. **Allow settling time**: Include delays after transitions to let current stabilize
   before the next transition.

3. **Control external factors**: Minimize external influences on power consumption:

   * Disable unused peripherals
   * Control environmental factors (temperature)
   * Ensure consistent supply voltage
   * Minimize current leakage paths

4. **Use realistic scenarios**: Test power consumption under actual use cases, not
   just theoretical states.

5. **Document expected values**: Comment your expected_rms_values with the rationale
   and how they were determined.

6. **Start with loose tolerances**: Begin with wider tolerances (25-30%) and tighten
   as you validate consistency.

7. **Calibrate your setup**: Measure known currents to verify your power monitor
   accuracy and account for measurement overhead.

8. **Consider temperature effects**: Power consumption varies with temperature.
   Control or account for thermal conditions.

9. **Test multiple samples**: Run power tests on multiple boards to account for
   unit-to-unit variation.

10. **Version control measurements**: Track expected values across firmware versions
    to detect power regressions.

Running Power Tests
*******************

Hardware Map Configuration
==========================

Configure your hardware map to include power monitoring fixtures:

.. code-block:: yaml

    - connected: true
      id: 683012345
      platform: nrf52840dk/nrf52840
      product: J-Link
      runner: nrfjprog
      serial: /dev/ttyACM0
      fixtures:
        - pm_probe
      probe_id: 683012345

Running with Twister
====================

Execute power tests through Twister using the hardware map:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: bash

         $ ./scripts/twister --device-testing --hardware-map hardware_map.yml \
           -T tests/power -p nrf52840dk/nrf52840

   .. group-tab:: Windows

      .. code-block:: bat

         python .\scripts\twister --device-testing --hardware-map hardware_map.yml ^
           -T tests\power -p nrf52840dk/nrf52840

Interpreting Results
********************

Understanding Test Output
=========================

When a power test runs, the harness reports:

* **Measured RMS values**: Actual current for each phase
* **Expected values**: Your configured expected_rms_values
* **Deviation**: Percentage difference from expected
* **Pass/Fail**: Whether measurements are within tolerance
* **Phase count**: Whether the expected number of transitions was detected

Example output:

.. code-block:: none

    Phase 1: 54.2mA (expected 56.0mA, -3.2%, PASS)
    Phase 2: 4.3mA (expected 4.0mA, +7.5%, PASS)
    Phase 3: 1.1mA (expected 1.2mA, -8.3%, PASS)
    Phase 4: 0.28mA (expected 0.26mA, +7.7%, PASS)
    Phase 5: 138.5mA (expected 140mA, -1.1%, PASS)

Debugging Failed Tests
***********************

When power tests fail:

1. **Review measured vs expected**: Check which phases failed and by how much.
   Small deviations suggest tolerance adjustment needed. Large deviations indicate
   actual power issues.

2. **Verify transition detection**: Ensure the correct number of transitions was
   detected. If wrong, adjust peak detection parameters.

3. **Check waveform data**: If available, plot the raw current waveform to visualize
   transitions and identify anomalies.

4. **Validate application behavior**: Verify the DUT is executing as expected and
   entering the intended power states.

5. **Review hardware setup**: Check connections, supply voltage, and measurement path.
   Ensure proper grounding and minimal parasitic effects.

6. **Consider environmental factors**: Temperature, supply voltage variations, and
   electromagnetic interference can affect results.

7. **Adjust signal processing parameters**: Fine-tune peak detection settings if
   transitions aren't being detected correctly.

8. **Check for power budget issues**: Verify the application hasn't introduced new
   power consumers or changed behavior.

Common Issues and Solutions
****************************

**Issue**: Wrong number of transitions detected
    **Solution**: Adjust ``min_peak_distance``, ``min_peak_height``, and ``peak_padding``
    parameters. Plot the waveform to see where transitions actually occur.

**Issue**: High variation in measurements
    **Solution**: Increase ``tolerance_percentage`` if variation is acceptable, or
    investigate sources of inconsistency (environmental, code, hardware).

**Issue**: Systematic offset in all measurements
    **Solution**: Verify power monitor calibration. Check for measurement shunt resistance
    or supply path resistance adding offset.

**Issue**: Noisy current measurements
    **Solution**: Increase ``element_to_trim`` to skip noisy startup. Check for
    electromagnetic interference. Verify measurement bandwidth is appropriate.

**Issue**: Test times out
    **Solution**: Ensure ``measurement_duration`` is appropriate. Check that the DUT
    application completes within the timeout period.

**Issue**: Power monitor not responding
    **Solution**: Verify hardware connection and driver installation. Check that the
    fixture is properly configured in hardware map.

Advanced Topics
***************

Custom Power Monitors
=====================

To integrate a new power monitoring device:

1. Implement the ``PowerMonitor`` abstract interface
2. Add hardware-specific measurement logic
3. Register the monitor with the harness
4. Update hardware map fixture configuration

Continuous Power Monitoring
============================

For long-term power profiling beyond test scenarios:

1. Use dedicated power profiling tools alongside Twister tests
2. Combine automated testing with manual analysis
3. Track power consumption trends across builds
4. Set up regression detection for power budgets

Integration with CI/CD
======================

Incorporate power testing in continuous integration:

1. Set up dedicated test hardware with power monitors
2. Configure automated hardware map updates
3. Track power metrics over time
4. Alert on power consumption regressions
5. Generate power consumption reports

Limitations
***********

Be aware of these limitations:

1. **Hardware dependency**: Requires specific power monitoring hardware
2. **Platform-specific**: Results are specific to the hardware being tested
3. **Environmental sensitivity**: Measurements affected by temperature, voltage, etc.
4. **Manual calibration**: Expected values must be determined empirically
5. **Limited phases**: Best suited for tests with clear, distinct power phases
6. **Measurement overhead**: Power monitor and connections add small overhead
7. **Sampling rate limits**: Very fast transitions may not be captured accurately

Additional Resources
********************

* :ref:`Zephyr Power Management <power_management_api>`
* :ref:`Pytest harness documentation <twister_pytest_harness>`
* `Power Profiling Best Practices <https://www.nordicsemi.com/Products/Development-tools/Power-Profiler-Kit-2>`_
