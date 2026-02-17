#!/usr/bin/env python3
# Copyright (c) 2026
#
# SPDX-License-Identifier: Apache-2.0

"""Blackbox tests for DT Doctor diagnostics from real Twister builds."""

import glob
import os

import pytest
from unittest import mock

# pylint: disable=no-name-in-module
from conftest import TEST_DATA, suite_filename_mock
from twisterlib.testplan import TestPlan
from twisterlib.twister_main import main as twister_main


class TestDTDoctor:
    TESTDATA = [
        (
            "dtdoctor.disabled_node",
            [
                "is disabled in",
                "Try enabling the node by setting its 'status' property to 'okay'.",
            ],
            [
                "is enabled but no driver appears to be available for it.",
            ],
        ),
        (
            "dtdoctor.enabled_no_driver",
            [
                "is enabled but no driver appears to be available for it.",
            ],
            [
                "is disabled in",
                "Try enabling the node by setting its 'status' property to 'okay'.",
            ],
        ),
    ]

    @pytest.mark.parametrize(
        "scenario, expected_fragments, unexpected_fragments",
        TESTDATA,
    )
    @mock.patch.object(TestPlan, "TESTSUITE_FILENAME", suite_filename_mock)
    def test_dtdoctor_build_failures(
        self,
        out_path,
        scenario,
        expected_fragments,
        unexpected_fragments,
    ):
        test_root = os.path.join(TEST_DATA, "tests", "dtdoctor")

        args = [
            "--outdir",
            out_path,
            "--build-only",
            "-T",
            test_root,
            "-p",
            "qemu_x86",
            "--test",
            scenario,
            "-x",
            "ZEPHYR_SCA_VARIANT=dtdoctor",
        ]

        return_code = twister_main(args)
        assert return_code == 1

        build_logs = glob.glob(os.path.join(out_path, "**", "build.log"), recursive=True)
        assert build_logs, "No build.log files found in Twister output"

        matching_log = None
        for build_log_path in build_logs:
            with open(build_log_path, encoding="utf-8") as build_log_file:
                contents = build_log_file.read()
            if "DT Doctor" in contents:
                matching_log = contents
                break

        assert matching_log is not None, "DT Doctor diagnostics were not emitted"
        assert "DT Doctor" in matching_log

        for fragment in expected_fragments:
            assert fragment in matching_log

        for fragment in unexpected_fragments:
            assert fragment not in matching_log
