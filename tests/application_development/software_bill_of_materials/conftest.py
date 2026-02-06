# Copyright (c) 2024 Basalte bv
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

import pytest


def pytest_addoption(parser):
    parser.addoption("--spdx-dir", required=True, help="SPDX output directory")
    parser.addoption("--spdx-version", default="2.3", help="Expected SPDX version")


@pytest.fixture
def spdx_dir(request):
    return Path(request.config.getoption("--spdx-dir"))


@pytest.fixture
def spdx_version(request):
    return request.config.getoption("--spdx-version")
