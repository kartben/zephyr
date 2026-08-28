#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0
"""Tests for the locators zspdx builds out of a west revision."""

import os
import sys

import pytest

ZEPHYR_BASE = os.getenv("ZEPHYR_BASE")
sys.path.insert(0, os.path.join(ZEPHYR_BASE, "scripts/pylib"))

from zspdx.serializers.helpers import (  # noqa: E402
    generate_download_url,
    resolvable_revision,
)

COMMIT = "563a6fdf8e4ad2c7c97df74657fbbc4803266ffc"


class TestResolvableRevision:
    # west suffixes a revision it could not confirm: "-off" when the checkout is
    # not on the manifest revision, "-dirty" when the tree was modified. The
    # commit in front of it is still the commit.
    @pytest.mark.parametrize("suffix", ["-off", "-dirty", "-dirty-off", "+dirty"])
    def test_the_commit_survives_a_west_suffix(self, suffix):
        assert resolvable_revision(COMMIT + suffix) == COMMIT

    def test_a_plain_commit_is_left_alone(self):
        assert resolvable_revision(COMMIT) == COMMIT

    def test_anything_that_is_not_a_commit_is_left_alone(self):
        # A tag or branch carries no commit to recover, and truncating it would
        # only invent a different, wrong name.
        assert resolvable_revision("v4.3.0") == "v4.3.0"
        assert resolvable_revision("main-off") == "main-off"
        assert resolvable_revision("") == ""


class TestDownloadUrl:
    def test_a_suffixed_revision_still_yields_a_fetchable_ref(self):
        assert generate_download_url("https://x/y", COMMIT + "-off") == f"git+https://x/y@{COMMIT}"

    def test_no_revision_leaves_the_url_bare(self):
        assert generate_download_url("https://x/y", "") == "https://x/y"
