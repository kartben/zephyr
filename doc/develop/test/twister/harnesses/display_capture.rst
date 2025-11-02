.. _twister_display_capture_harness:

Display Capture Harness
#######################

The ``display_capture`` harness enables automated visual testing of display drivers by
capturing and analyzing display output using a camera. This harness integrates with pytest
to perform automated visual validation using video fingerprinting techniques, making it
possible to detect display rendering issues, color problems, and visual regressions.

Overview
********

Visual testing of displays is challenging to automate because it requires capturing and
analyzing what the display actually shows, not just what the software intends to show. The
display_capture harness addresses this by:

- Using a camera to capture the physical display output
- Generating visual "fingerprints" from captured video  
- Comparing fingerprints against known-good reference fingerprints
- Reporting pass/fail based on visual similarity metrics

This enables automated detection of:

- Rendering errors and graphical glitches
- Color accuracy issues
- Brightness and contrast problems
- Layout and positioning errors
- Visual regressions between code versions

.. figure:: ../figures/twister_display_capture_success.webp
   :align: center
   :alt: A window showing a camera preview of a device display with colored blocks in the corners,
         with a text overlay indicating a successful test match.

   Window displayed during a "compare" run where fingerprint is a 90% match with the reference.

How It Works
************

The display capture harness uses a two-phase workflow:

**Phase 1: Reference Generation**
    Capture video of a known-good display output and generate reference fingerprints
    that represent the expected visual appearance.

**Phase 2: Validation**
    For each test run, capture new video and generate fingerprints, then compare against
    the reference fingerprints using multiple similarity metrics (perceptual hash,
    difference hash, color histogram, edge detection, gradient analysis).

If the similarity score exceeds the configured threshold, the visual output is considered
correct and the test passes.

Hardware Requirements
*********************

The display capture harness requires specific hardware setup:

Camera
======

- **UVC compatible camera** with at least 2 megapixels (e.g., 1080p resolution)
- Must support standard UVC (USB Video Class) protocol
- Higher resolution cameras provide better accuracy but slower processing

Environment
===========

- **Light-blocking enclosure** or black curtain for consistent lighting conditions
- Eliminates ambient light variations that affect measurements
- Ensures repeatable capture conditions

Connections
===========

- **PC host** with camera connection for capturing display output
- **DUT** (Device Under Test) connected to the same PC for flashing and serial console
- Stable camera mounting to minimize position variations

Configuration
*************

The harness uses a YAML configuration file to define camera settings, test parameters,
and fingerprint analysis options.

Configuration File Structure
=============================

.. code-block:: yaml
   :caption: display_config.yaml

   case_config:
     device_id: 0
     res_x: 1280
     res_y: 720
     fps: 30
     run_time: 20
   tests:
     timeout: 30
     prompt: "screen starts"
     expect: ["tests.drivers.display.check.shield"]
   plugins:
     - name: signature
       module: plugins.signature_plugin
       class: VideoSignaturePlugin
       status: enable
       config:
         operations: "compare"  # or "generate"
         metadata:
           name: "tests.drivers.display.check.shield"
           platform: "frdm_mcxn947"
         directory: "./fingerprints"
         duration: 100
         method: "combined"
         threshold: 0.65
         phash_weight: 0.35
         dhash_weight: 0.25
         histogram_weight: 0.2
         edge_ratio_weight: 0.1
         gradient_hist_weight: 0.1

Configuration Sections
======================

case_config
-----------

Defines camera settings and test duration:

device_id: <integer|string|URL>
    Camera device identifier. Can be:
    
    - Integer for local cameras (0 for first camera, 1 for second, etc.)
    - Device path string (e.g., ``/dev/video0`` on Linux)
    - IP video stream URL (e.g., ``rtsp://192.168.1.100:8554/stream``)

res_x: <integer> (default 1280)
    Horizontal resolution in pixels

res_y: <integer> (default 720)  
    Vertical resolution in pixels

fps: <integer> (default 30)
    Frames per second for video capture

run_time: <integer> (default 20)
    Duration of test in seconds

tests
-----

Device interaction configuration:

timeout: <integer> (default 30)
    Maximum seconds to wait for prompt in UART output

prompt: <string> (default "uart:~$")
    Pattern to wait for in UART output before starting capture (supports regex)

expect: <list of strings> (default ["PASS"])
    Expected test result strings to match against application output

plugins
-------

Video analysis plugin configuration. Currently supports ``VideoSignaturePlugin``:

operations: <"generate"|"compare">
    Operating mode:
    
    - ``generate``: Capture and save reference fingerprints
    - ``compare``: Compare captured fingerprints against references

metadata:
    Optional identification information:
    
    - ``name``: Test case identifier
    - ``platform``: Target platform identifier

directory: <string> (default "./fingerprints")
    Directory for storing/loading fingerprints

duration: <integer>
    Number of frames to analyze (more frames = slower but more accurate)

method: <"phash"|"dhash"|"histogram"|"combined">
    Fingerprint generation method:
    
    - ``phash`` (Perceptual Hash): Captures overall structure, detects layout issues
    - ``dhash`` (Difference Hash): Detects brightness/contrast problems
    - ``histogram`` (Color Histogram): Fast color problem detection
    - ``combined`` (recommended): Weighted combination of all methods

threshold: <float> (default 0.65)
    Similarity threshold for pass/fail (0.0-1.0, higher = more strict)

\*_weight: <float>
    Weights for combined method (phash_weight, dhash_weight, histogram_weight,
    edge_ratio_weight, gradient_hist_weight). Must sum to 1.0.

Test Configuration
==================

Reference the configuration file in ``testcase.yaml``:

.. code-block:: yaml

   tests:
     drivers.display.test:
       harness: display_capture
       harness_config:
         pytest_dut_scope: session
         fixture: fixture_display
         display_capture_config: "${DISPLAY_TEST_DIR}/display_config.yaml"

The ``DISPLAY_TEST_DIR`` environment variable points to the directory containing
the configuration file.

Workflow
********

Generating Reference Fingerprints
==================================

First-time setup to create reference fingerprints:

.. code-block:: bash

   # Build and flash test application
   west build -b <board> tests/drivers/display/display_check
   west flash

   # Configure for fingerprint generation
   # Edit display_config.yaml: set operations: "generate"

   # Generate reference fingerprints
   export DISPLAY_TEST_DIR=<path-to-config-directory>
   scripts/twister --device-testing --hardware-map map.yml \
       -T tests/drivers/display/display_check/

Fingerprints are saved in the configured ``directory``, organized by test name
and platform from the ``metadata`` section.

Running Validation Tests
=========================

Once reference fingerprints exist, run validation:

.. code-block:: bash

   # Configure for comparison mode
   # Edit display_config.yaml: set operations: "compare"

   # Run validation tests
   export DISPLAY_TEST_DIR=<path-to-config-directory>
   scripts/twister --device-testing --hardware-map map.yml \
       -T tests/drivers/display/display_check/

The harness compares new captures against references and reports pass/fail based on
the similarity threshold.

Fingerprint Methods
*******************

Each fingerprint method detects different types of visual issues:

Perceptual Hash (phash)
=======================

Captures overall visual structure and layout. Best for detecting:

- UI elements positioned incorrectly
- Missing or extra graphical elements
- Major layout changes

Difference Hash (dhash)
=======================

Detects brightness patterns and gradients. Sensitive to:

- Brightness or contrast problems
- Gradient rendering issues
- Lighting changes

Color Histogram
===============

Analyzes color distribution. Fast at detecting:

- Color swap bugs
- Incorrect color values
- Overall color balance issues

Combined Method
===============

Recommended approach that uses weighted combination of all methods plus edge detection
and gradient analysis. Provides balanced detection of both major and subtle visual issues.

Best Practices
**************

1. **Stable lighting**: Use light-blocking enclosure to eliminate ambient light variations.

2. **Fixed camera position**: Mount camera securely to avoid position changes between runs.

3. **Appropriate threshold**: Start with default (0.65) and adjust based on your needs.
   Lower = more tolerant, higher = more strict.

4. **Sufficient frames**: Use enough frames (duration parameter) for reliable fingerprints,
   but balance against test execution time.

5. **Multiple references**: Consider generating fingerprints for different valid outputs
   if your display can show variations.

6. **Document references**: Keep notes on when references were generated and what they
   represent.

7. **Version control**: Store reference fingerprints in version control to track
   expected visual changes over time.

Troubleshooting
***************

**Tests failing with good output**
    - Reduce ``threshold`` to be more tolerant
    - Check for lighting or camera position changes
    - Regenerate reference fingerprints in current environment

**Tests passing with bad output**
    - Increase ``threshold`` to be more strict
    - Use ``combined`` method instead of single method
    - Increase ``duration`` for more samples

**Inconsistent results**
    - Improve lighting consistency (use enclosure)
    - Secure camera mounting
    - Check for environmental vibrations

**Slow test execution**
    - Reduce ``duration`` (fewer frames)
    - Lower camera resolution
    - Use faster fingerprint method (histogram)

Important Notes
***************

.. note::

   - Test name in ``testcase.yaml`` must match ``name`` in fingerprint metadata
   - Multiple fingerprints can be stored in one directory (increases comparison time)
   - Fingerprints are specific to both test scenario and platform
   - Reference fingerprints should be regenerated when expected output changes

Use Cases
*********

The display_capture harness is valuable for:

**Driver Validation**
    Verify display drivers render correctly across different configurations.

**Regression Testing**
    Detect visual regressions introduced by code changes.

**Color Accuracy**
    Validate color rendering meets requirements.

**Layout Verification**
    Ensure UI elements appear in correct positions.

**Multi-Platform Testing**
    Verify consistent rendering across different display hardware.

See Also
********

- :ref:`twister_pytest_harness` - Pytest harness documentation
- :ref:`twister_harnesses` - Overview of all available harnesses
- :ref:`twister_script` - Main Twister documentation
