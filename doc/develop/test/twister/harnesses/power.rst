.. _twister_power_harness:

Power Harness
#############

The ``power`` harness enables automated measurement and validation of current consumption
in embedded devices. By integrating hardware power monitoring equipment with pytest-based
test automation, this harness allows you to verify that your application meets power
consumption requirements and detect power-related regressions.

Overview
********

Power consumption is critical for battery-powered embedded devices. The power harness
automates the process of:

- Measuring current consumption during test execution
- Segmenting measurements into distinct execution phases
- Calculating RMS (Root Mean Square) current values for each phase
- Validating actual consumption against expected values
- Detecting power consumption regressions

This harness is particularly valuable for validating power management implementations,
low-power modes, and energy optimization efforts.

How It Works
************

The power harness executes the following workflow:

1. Initializes a power monitoring device (e.g., ``stm_powershield``) through the
   ``PowerMonitor`` abstract interface
2. Starts current measurement for a configured duration
3. Collects raw current waveform data from the monitoring hardware
4. Uses peak detection algorithms to identify execution phase transitions based on
   current changes
5. Computes RMS current values for each detected phase
6. Compares measured values against expected RMS values within tolerance
7. Reports pass/fail based on whether all phases meet their power requirements

Integration
***********

The power harness integrates with the pytest framework, leveraging pytest's test
organization and reporting capabilities while adding power measurement functionality.
This allows combining power validation with other pytest-based testing.

Configuration
*************

The power harness requires detailed configuration of measurement parameters:

.. code-block:: yaml

   tests:
     power.consumption.test:
       harness: power
       harness_config:
         fixture: pm_probe
         power_measurements:
           element_to_trim: 100
           min_peak_distance: 40
           min_peak_height: 0.008
           peak_padding: 40
           measurement_duration: 6
           num_of_transitions: 4
           expected_rms_values: [56.0, 4.0, 1.2, 0.26, 140]
           tolerance_percentage: 20

Configuration Parameters
************************

fixture: <string>
    Specifies the hardware power monitoring fixture to use (e.g., ``pm_probe``).
    This must match a fixture definition in your hardware map.

power_measurements: <configuration object>
    Contains all power measurement parameters:

element_to_trim: <integer>
    Number of samples to discard at the beginning of the measurement to eliminate
    initial noise and stabilization artifacts. This helps ensure clean data for analysis.

min_peak_distance: <integer>
    Minimum number of samples between detected current peaks. This parameter helps
    distinguish between distinct power state transitions and prevents multiple detections
    of the same transition.

min_peak_height: <float>
    Minimum current threshold (in amps) for a change to qualify as a peak. Lower values
    detect smaller transitions; higher values ignore minor fluctuations.

peak_padding: <integer>
    Number of samples to extend around each detected peak. This ensures the full
    transition is captured, not just the peak point.

measurement_duration: <integer>
    Total time in seconds to record current data. Should be long enough to capture
    all expected execution phases.

num_of_transitions: <integer>
    Expected number of power state transitions during test execution. The algorithm
    uses this to segment the current waveform into distinct phases.

expected_rms_values: <list of floats>
    Target RMS current values in milliamps for each execution phase, in order.
    The number of values should match the number of distinct phases in your test.

tolerance_percentage: <float>
    Allowed deviation percentage from expected RMS values. For example, 20 means
    actual values can be ±20% of expected values.

Example Scenario
****************

Consider a device that transitions through these power states:

1. **Active processing**: ~56 mA
2. **Idle with peripherals**: ~4 mA  
3. **Light sleep**: ~1.2 mA
4. **Deep sleep**: ~0.26 mA
5. **Wake and active**: ~140 mA

The power harness configuration would specify:

.. code-block:: yaml

   power_measurements:
     measurement_duration: 10
     num_of_transitions: 4
     expected_rms_values: [56.0, 4.0, 1.2, 0.26, 140]
     tolerance_percentage: 20
     # ... other parameters

The harness measures current over 10 seconds, identifies the 4 transitions between
states, calculates RMS current for each of the 5 phases, and validates they're
within ±20% of expected values.

Hardware Setup
**************

To use the power harness, you need:

1. **Power monitoring hardware**: A device capable of precise current measurement
   (e.g., STM PowerShield, Nordic PPK2, or similar)

2. **Hardware map configuration**: Your hardware map must define the power monitoring
   fixture:

   .. code-block:: yaml

      - connected: true
        platform: nrf52840dk/nrf52840
        fixtures:
          - pm_probe
        serial: /dev/ttyACM0
        # ... other configuration

3. **PowerMonitor interface**: The monitoring hardware must be accessible through
   Twister's PowerMonitor abstract interface.

Use Cases
*********

The power harness is ideal for:

**Power Budget Validation**
    Verify that different execution states meet power consumption requirements.

**Low-Power Mode Testing**
    Validate that sleep modes achieve expected current draw.

**Regression Detection**
    Detect power consumption increases caused by code changes.

**Power Profile Documentation**
    Generate documented measurements of application power consumption.

**Optimization Validation**
    Verify that power optimization efforts produce measurable improvements.

Best Practices
**************

1. **Stable environment**: Perform measurements in a controlled environment with
   stable power supply and minimal electrical noise.

2. **Adequate duration**: Set ``measurement_duration`` long enough to capture all
   phases with sufficient samples for accurate RMS calculation.

3. **Appropriate tolerance**: Set ``tolerance_percentage`` based on your measurement
   equipment accuracy and acceptable variation.

4. **Trim initialization**: Use ``element_to_trim`` to remove startup transients
   that could skew measurements.

5. **Peak detection tuning**: Adjust ``min_peak_distance`` and ``min_peak_height``
   based on your application's power profile characteristics.

6. **Document transitions**: Clearly document in your test what each phase represents
   to make results interpretable.

7. **Consistent setup**: Use the same hardware and setup for comparable measurements
   across test runs.

Limitations
***********

- Requires specialized power monitoring hardware
- Measurements can be affected by environmental factors
- Peak detection may struggle with very gradual transitions
- Not suitable for extremely short power events
- Requires careful calibration of detection parameters

Troubleshooting
***************

**Incorrect phase detection**
    Adjust ``min_peak_distance`` and ``min_peak_height`` to better match your
    application's power transitions.

**Noisy measurements**
    Increase ``element_to_trim`` or check hardware connections and grounding.

**Failing tolerance checks**
    Verify expected values are realistic, check for hardware issues, or adjust
    ``tolerance_percentage`` if variation is expected.

**Missing transitions**
    Increase ``measurement_duration`` or check that the application is executing
    as expected.

See Also
********

- :ref:`twister_pytest_harness` - Pytest harness documentation (power harness uses pytest)
- :ref:`twister_harnesses` - Overview of all available harnesses
- :ref:`twister_script` - Main Twister documentation
