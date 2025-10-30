.. zephyr:code-sample:: pytest_display_capture
   :name: Pytest display_capture harness demo
   :relevant-api: display_interface

   Demonstrate the display_capture harness for testing display applications.

Overview
********

This sample project illustrates usage of the ``display_capture`` harness
integrated with Twister test runner. The display_capture harness enables
automated testing of display functionality by capturing video output from
the display and comparing it against known good fingerprints.

The sample provides a simple display application that draws a colored rectangle
on the screen. Twister can build this application and test it using either:

1. **Basic pytest harness**: Simple functional test without video capture
2. **Display_capture harness**: Full video capture and fingerprint comparison
   (requires camera setup)

How the Display_capture Harness Works
**************************************

The display_capture harness workflow:

1. **Build and Flash**: Twister builds and flashes the display application
2. **Wait for Ready**: The harness waits for a configured prompt (e.g., "Display starts")
3. **Video Capture**: A camera captures the display output for a configured duration
4. **Fingerprint Comparison**: The captured video is compared against pre-generated
   fingerprints using various image similarity algorithms
5. **Pass/Fail**: The test passes if the similarity score exceeds the threshold

Components
**********

Application Code
================

The application (``src/main.c``) demonstrates:

- Display device initialization
- Buffer allocation and configuration
- Drawing colored rectangles
- Proper error handling for missing displays

Pytest Test
===========

The test (``pytest/test_display_demo.py``) shows:

- How to use the ``DeviceAdapter`` fixture from twister_harness
- Waiting for application readiness using ``readlines_until()``
- Basic test structure for display applications

Configuration
=============

``testcase.yaml`` shows two test configurations:

- **sample.pytest.display_capture**: Basic test using pytest harness
- **sample.pytest.display_capture.with_camera**: Example using display_capture
  harness (commented out, requires setup)

``display_config.yaml`` demonstrates:

- Camera configuration (device, resolution, FPS)
- Test parameters (timeout, prompt, expected results)
- Fingerprint comparison settings (method, threshold, weights)

Requirements
************

Basic Test (pytest harness)
============================

- Board with display support (e.g., ``native_sim`` for emulation)
- No camera required

Full Display Capture Test
==========================

- Hardware board with display (e.g., ``frdm_mcxn947`` with ``lcd_par_s035_8080`` shield)
- USB camera (UVC compatible, at least 1080p recommended)
- Camera positioned to capture the display
- Light-blocking enclosure (black curtain recommended)
- Python packages from ``scripts/pylib/display-twister-harness/camera_shield/requirements.txt``

Building and Running
********************

Basic Test (No Camera Required)
================================

Run the basic test on native_sim:

.. code-block:: console

   $ ./scripts/twister -vv --platform native_sim -T samples/subsys/testsuite/pytest/display_capture

This will run the pytest test without video capture.

Setting Up Display Capture Testing
===================================

1. **Hardware Setup**:

   - Connect your display board (e.g., FRDM-MCXN947 with LCD shield)
   - Position camera to capture the display
   - Create a light-blocking enclosure around the camera and display
   - Ensure stable lighting conditions

2. **Install Dependencies**:

   .. code-block:: console

      $ cd scripts/pylib/display-twister-harness/camera_shield
      $ pip install -r requirements.txt

3. **Generate Fingerprints**:

   First, create a known-good reference by running your application and
   generating fingerprints:

   a. Build and flash your application:

      .. code-block:: console

         $ west build -b frdm_mcxn947/mcxn947/cpu0 \
           samples/subsys/testsuite/pytest/display_capture \
           -- -DSHIELD=lcd_par_s035_8080
         $ west flash

   b. Set up camera_shield configuration for fingerprint generation:

      Create or modify ``display_config.yaml`` to use ``operations: "generate"``:

      .. code-block:: yaml

         plugins:
           - name: signature
             config:
               operations: "generate"  # Changed from "compare"
               metadata:
                 name: "sample.pytest.display_capture"
                 platform: "frdm_mcxn947"

   c. Run camera_shield to generate fingerprints:

      .. code-block:: console

         $ python -m camera_shield.main --config display_config.yaml

      This creates fingerprints in the ``./fingerprints`` directory.

4. **Configure Environment**:

   .. code-block:: console

      $ export DISPLAY_TEST_DIR=/path/to/fingerprints/parent/directory

5. **Enable Display Capture Test**:

   Edit ``testcase.yaml`` and uncomment the ``sample.pytest.display_capture.with_camera``
   test, adjusting platform and shield as needed.

6. **Run Test with Display Capture**:

   .. code-block:: console

      $ ./scripts/twister --device-testing --hardware-map map.yml \
        -T samples/subsys/testsuite/pytest/display_capture

   Your ``map.yml`` should include the required fixture:

   .. code-block:: yaml

      - connected: true
        id: your_board_id
        platform: frdm_mcxn947/mcxn947/cpu0
        product: FRDM-MCXN947
        runner: jlink
        fixtures:
          - fixture_display

Sample Output
*************

Basic pytest test output:

.. code-block:: console

   ...
   samples/subsys/testsuite/pytest/display_capture/pytest/test_display_demo.py::test_display_demo
   INFO: Starting display capture demo test
   DEBUG: Display sample for DISPLAY
   DEBUG: Display resolution: 320x240
   DEBUG: Display starts
   INFO: Display application started successfully
   PASSED
   ...

Display capture test output (when using camera):

.. code-block:: console

   ...
   INFO: Starting display capture demo test
   INFO: Waiting for prompt: Display starts
   DEBUG: Display starts
   INFO: Starting video capture
   INFO: Analyzing captured frames
   INFO: Comparing against fingerprints
   INFO: Similarity score: 0.87 (threshold: 0.65)
   PASSED
   ...

Troubleshooting
***************

Camera Not Found
================

If you get "camera not found" errors:

- Try different values for ``device_id`` in ``display_config.yaml`` (0, 1, 2, etc.)
- Check camera permissions: ``sudo usermod -a -G video $USER``
- Verify camera with: ``ls /dev/video*``

Low Similarity Scores
=====================

If tests fail due to low similarity:

- Ensure stable lighting conditions
- Check camera focus and positioning
- Verify the correct fingerprints are being used
- Consider adjusting the threshold in ``display_config.yaml``
- Regenerate fingerprints if hardware or setup changed

Display Issues on Linux
========================

For display issues with opencv:

.. code-block:: console

   $ export QT_QPA_PLATFORM=offscreen
   $ pip install opencv-python-headless

References
**********

- Display capture harness documentation:
  ``scripts/pylib/display-twister-harness/camera_shield/README.rst``
- Pytest harness documentation: ``doc/develop/test/pytest.rst``
- Display driver API: ``include/zephyr/drivers/display.h``
- Example display test: ``tests/drivers/display/display_check/``
