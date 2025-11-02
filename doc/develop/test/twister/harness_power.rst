.. _twister_harness_power:

Power Harness
#############

.. _twister_power_harness:

Overview
********

The Power harness is a specialized testing tool designed to measure, validate, and analyze
the current consumption of Zephyr applications. It integrates with hardware power monitoring
devices and uses the pytest framework to perform automated data collection and sophisticated
analysis of power consumption patterns.

This harness is essential for:

- **Power optimization** - Validating that power-saving features work as expected
- **Battery life estimation** - Measuring actual power consumption for battery-powered devices
- **Power state verification** - Confirming correct transitions between power states
- **Regression testing** - Ensuring power optimizations don't regress over time
- **Compliance validation** - Verifying power consumption meets specification requirements

The Power harness uses advanced signal processing techniques to automatically detect power
state transitions and calculate accurate current measurements for each operational phase.

How It Works
************

The Power harness implements a sophisticated measurement and analysis workflow:

1. **Initialization**: Sets up communication with a power monitoring device (e.g., ``stm_powershield``)
   through the ``PowerMonitor`` abstract interface

2. **Data Collection**: Captures raw current waveform data for a specified duration while the
   test application runs on the target device

3. **Signal Processing**: Uses a peak detection algorithm to automatically identify transitions
   between different power states based on current level changes

4. **Phase Segmentation**: Divides the captured data into distinct execution phases based on
   the detected transitions

5. **RMS Calculation**: Computes Root Mean Square (RMS) current values for each identified phase,
   providing accurate power consumption measurements

6. **Validation**: Compares measured RMS values against expected values with configurable
   tolerance, reporting pass/fail results

This automated approach eliminates manual analysis and provides consistent, repeatable
power measurements.

Hardware Requirements
*********************

To use the Power harness, you need:

- **Power Monitoring Device**: A compatible hardware power monitor (e.g., STM Power Shield)
- **Device Under Test**: The Zephyr board being measured
- **Test Fixture**: Specified using the ``fixture: pm_probe`` configuration
- **Proper Wiring**: Correct connections between power monitor and DUT

The power monitor must support the ``PowerMonitor`` abstract interface for integration
with the harness.

Configuration
*************

The Power harness requires detailed configuration to properly capture and validate
power measurements:

Basic Configuration
===================

.. code-block:: yaml

    tests:
      power.consumption.test:
        harness: power
        harness_config:
          fixture: pm_probe
          power_measurements:
            measurement_duration: 6
            expected_rms_values: [56.0, 4.0, 1.2, 0.26, 140]
            tolerance_percentage: 20

Power Measurement Parameters
=============================

All parameters are specified within the ``power_measurements`` section:

**element_to_trim: <int>**
    Number of samples to discard at the beginning of the measurement to eliminate
    startup noise and transients. This helps ensure clean data for analysis.
    
    Example: ``element_to_trim: 100``

**min_peak_distance: <int>**
    Minimum number of samples between detected current peaks. This parameter helps
    the algorithm distinguish between separate power state transitions rather than
    noise within a single state.
    
    Example: ``min_peak_distance: 40``

**min_peak_height: <float>**
    Minimum current threshold (in amps) for a signal change to qualify as a peak.
    This filters out small noise fluctuations and only detects significant power
    state transitions.
    
    Example: ``min_peak_height: 0.008`` (8 mA)

**peak_padding: <int>**
    Number of samples to include around each detected peak. This extends the
    region of interest to capture the full transition between states.
    
    Example: ``peak_padding: 40``

**measurement_duration: <int>**
    Total time in seconds to record current data from the device. Should be long
    enough to capture all expected power states and transitions.
    
    Example: ``measurement_duration: 6``

**num_of_transitions: <int>**
    Expected number of power state transitions during the test. This helps validate
    that the test executed correctly and all states were entered.
    
    Example: ``num_of_transitions: 4``

**expected_rms_values: <list of floats>**
    Target RMS current values in milliamps for each identified execution phase.
    The list should contain one value for each expected phase (num_of_transitions + 1).
    
    Example: ``expected_rms_values: [56.0, 4.0, 1.2, 0.26, 140]``

**tolerance_percentage: <float>**
    Allowed deviation percentage from the expected RMS values. Measured values
    within ±tolerance_percentage of expected values will pass.
    
    Example: ``tolerance_percentage: 20`` (allows ±20% deviation)

Complete Example
****************

Here's a comprehensive power measurement test configuration:

**testcase.yaml**:

.. code-block:: yaml

    tests:
      power.states.validation:
        harness: power
        platform: nrf52840dk/nrf52840
        tags: power low_power
        harness_config:
          fixture: pm_probe
          power_measurements:
            # Data preprocessing
            element_to_trim: 100
            
            # Peak detection parameters
            min_peak_distance: 40
            min_peak_height: 0.008
            peak_padding: 40
            
            # Measurement parameters
            measurement_duration: 6
            num_of_transitions: 4
            
            # Validation criteria
            expected_rms_values: [56.0, 4.0, 1.2, 0.26, 140]
            tolerance_percentage: 20

**src/main.c**:

.. code-block:: c

    #include <zephyr/kernel.h>
    #include <zephyr/pm/pm.h>

    void main(void)
    {
        printk("Power state test starting\n");
        
        // Active state - high power
        k_busy_wait(1000000);  // 1 second
        
        // Light sleep - medium power
        pm_state_force(PM_STATE_SUSPEND_TO_IDLE);
        k_sleep(K_SECONDS(1));
        
        // Deep sleep - low power
        pm_state_force(PM_STATE_SUSPEND_TO_RAM);
        k_sleep(K_SECONDS(2));
        
        // Ultra-low power state
        pm_state_force(PM_STATE_SOFT_OFF);
        k_sleep(K_SECONDS(1));
        
        // Return to active - high power
        k_busy_wait(1000000);
        
        printk("Power state test complete\n");
    }

This test transitions through five distinct power states (initial active, light sleep,
deep sleep, ultra-low power, final active), creating four transitions. The expected_rms_values
array corresponds to the current consumption in each of these five states.

Understanding Results
*********************

The Power harness provides detailed results including:

- **Phase Identification**: Which phases were detected and their boundaries
- **RMS Current Values**: Measured RMS current for each phase
- **Comparison**: Expected vs. actual values for each phase
- **Pass/Fail Status**: Whether each phase meets tolerance requirements
- **Overall Result**: Whether the complete test passed or failed

Example output (conceptual):

.. code-block:: none

    Phase 1: 54.3 mA (expected 56.0 mA) - PASS (within 20% tolerance)
    Phase 2: 4.2 mA (expected 4.0 mA) - PASS
    Phase 3: 1.1 mA (expected 1.2 mA) - PASS
    Phase 4: 0.28 mA (expected 0.26 mA) - PASS
    Phase 5: 138.5 mA (expected 140 mA) - PASS
    
    Overall: PASS (all phases within tolerance)

Tuning Parameters
*****************

Getting accurate results requires tuning the parameters for your specific device and test:

1. **Start with a Capture**: Run the test with generous tolerances to see actual values

2. **Adjust Peak Detection**: If transitions aren't detected correctly:
   
   - Increase ``min_peak_height`` if noise is causing false detections
   - Decrease ``min_peak_height`` if real transitions are missed
   - Adjust ``min_peak_distance`` based on how long each state lasts

3. **Refine Expectations**: Update ``expected_rms_values`` based on measured values

4. **Set Realistic Tolerances**: Account for:
   
   - Measurement device accuracy
   - Environmental factors (temperature, voltage variations)
   - Device-to-device variation
   - Timing variations in the test

5. **Validate Transitions**: Ensure ``num_of_transitions`` matches your test design

Best Practices
**************

1. **Design Clear Power States**: Make power state transitions distinct and measurable

2. **Allow Stabilization Time**: Give each power state enough time to stabilize before
   transitioning to the next state

3. **Minimize External Factors**: Control environmental conditions during testing:
   
   - Consistent temperature
   - Stable power supply voltage
   - Minimal electromagnetic interference

4. **Calibrate Equipment**: Regularly calibrate power monitoring equipment

5. **Document Expectations**: Clearly document why specific RMS values are expected
   for each phase

6. **Test Repeatability**: Run tests multiple times to ensure consistent results

7. **Version Control**: Track power measurements over time to detect regressions

Troubleshooting
***************

Common Issues
=============

**Wrong Number of Transitions Detected**
    - Verify ``min_peak_height`` is appropriate for your current levels
    - Check ``min_peak_distance`` allows sufficient separation
    - Review the raw data capture to see what the algorithm sees
    - Ensure the test application is executing all expected states

**RMS Values Outside Tolerance**
    - Verify the test is actually executing correctly
    - Check for environmental factors affecting measurements
    - Ensure proper connections between power monitor and DUT
    - Consider if ``tolerance_percentage`` is too strict

**No Data Captured**
    - Verify power monitor is properly connected and recognized
    - Check that the ``fixture: pm_probe`` is correctly configured in hardware map
    - Ensure the power monitor driver is working correctly
    - Verify measurement_duration is sufficient

**Inconsistent Results**
    - Look for external interference or unstable power supply
    - Check for temperature variations between test runs
    - Ensure test execution is deterministic
    - Consider using longer ``measurement_duration`` for better statistics

Advanced Usage
**************

For sophisticated power analysis:

- Combine with :ref:`Console harness <twister_harness_console>` to correlate power
  states with specific application behaviors
  
- Use multiple test scenarios to validate different power modes independently

- Integrate with CI/CD to detect power regressions automatically

- Export measurement data for offline analysis and visualization

See Also
********

- :ref:`Pytest Harness <twister_harness_pytest>` (Power harness uses pytest internally)
- :ref:`Test Configuration <test_config_args>`
- :ref:`Power Management <power_management>` in Zephyr
- Hardware Map Configuration for test fixtures
