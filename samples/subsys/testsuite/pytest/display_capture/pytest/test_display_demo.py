# Copyright (c) 2025 Contributors to the Zephyr Project
#
# SPDX-License-Identifier: Apache-2.0

"""
This is a sample pytest test demonstrating the display_capture harness.

The display_capture harness allows testing display functionality by:
1. Running a display application on the device
2. Capturing the display output using a camera
3. Comparing the captured video against known good fingerprints

This test is a simple demonstration showing how to use the harness.
For real testing scenarios, you would set up fingerprints as described
in the camera_shield README.
"""

import logging

from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)


def test_display_demo(dut: DeviceAdapter):
    """
    Simple test that verifies the display application starts.

    In a real scenario with display_capture_config and fingerprints set up,
    this would trigger video capture and fingerprint comparison.
    """
    logger.info("Starting display capture demo test")

    # Wait for the display to initialize and show the prompt
    dut.readlines_until(
        regex="Display starts",
        timeout=30,
        print_output=True,
    )

    logger.info("Display application started successfully")
