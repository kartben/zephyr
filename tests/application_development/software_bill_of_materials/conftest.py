# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Pytest configuration for SPDX content validation tests."""

import json
import os

import pytest
from packaging import version

from spdx_adapter import parse_spdx


def pytest_addoption(parser):
    """Add command-line options for pytest."""
    parser.addoption(
        "--build-dir",
        action="store",
        required=True,
        help="Path to the build directory containing SPDX files",
    )
    parser.addoption(
        "--spdx-version",
        action="store",
        required=True,
        help="Expected SPDX version (e.g., '2.2', '2.3', or '3.0')",
    )
    parser.addoption(
        "--source-dir",
        action="store",
        required=True,
        help="Path to the test source directory containing src/main.c",
    )


def pytest_configure(config):
    """Register custom markers."""
    config.addinivalue_line(
        "markers",
        "min_spdx_version(version): skip test if SPDX version is less than specified",
    )
    config.addinivalue_line(
        "markers",
        "max_spdx_version(version): skip test if SPDX version is greater than specified",
    )


def pytest_runtest_setup(item):
    """Skip tests based on min/max spdx_version markers."""
    current = version.parse(item.config.getoption("--spdx-version"))

    marker = item.get_closest_marker("min_spdx_version")
    if marker is not None:
        min_ver = version.parse(marker.args[0])
        if current < min_ver:
            pytest.skip(f"Requires SPDX version >= {min_ver}, got {current}")

    marker = item.get_closest_marker("max_spdx_version")
    if marker is not None:
        max_ver = version.parse(marker.args[0])
        if current > max_ver:
            pytest.skip(f"Requires SPDX version <= {max_ver}, got {current}")


@pytest.fixture(scope="session")
def build_dir(request):
    """Fixture providing the build directory path."""
    return request.config.getoption("--build-dir")


@pytest.fixture(scope="session")
def spdx_version(request):
    """Fixture providing the expected SPDX version."""
    return request.config.getoption("--spdx-version")


@pytest.fixture(scope="session")
def source_dir(request):
    """Fixture providing the test source directory path."""
    return request.config.getoption("--source-dir")


@pytest.fixture(scope="session")
def spdx_dir(build_dir):
    """Fixture providing the SPDX directory path."""
    return os.path.join(build_dir, "spdx")


def _spdx_file_path(spdx_dir, doc_name, spdx_version):
    """Return the correct file path for a given SPDX document and version."""
    if spdx_version.startswith("3"):
        return os.path.join(spdx_dir, f"{doc_name}-spdx3.jsonld")
    return os.path.join(spdx_dir, f"{doc_name}.spdx")


@pytest.fixture(scope="session")
def app_doc(spdx_dir, spdx_version):
    """Fixture providing the parsed app document."""
    return parse_spdx(_spdx_file_path(spdx_dir, "app", spdx_version), spdx_version)


@pytest.fixture(scope="session")
def zephyr_doc(spdx_dir, spdx_version):
    """Fixture providing the parsed zephyr document."""
    return parse_spdx(_spdx_file_path(spdx_dir, "zephyr", spdx_version), spdx_version)


@pytest.fixture(scope="session")
def build_doc(spdx_dir, spdx_version):
    """Fixture providing the parsed build document."""
    return parse_spdx(_spdx_file_path(spdx_dir, "build", spdx_version), spdx_version)


@pytest.fixture(scope="session")
def modules_doc(spdx_dir, spdx_version):
    """Fixture providing the parsed modules-deps document."""
    return parse_spdx(
        _spdx_file_path(spdx_dir, "modules-deps", spdx_version), spdx_version
    )


# ---------------------------------------------------------------------------
# Raw JSON-LD fixtures for SPDX 3.0-specific tests
# ---------------------------------------------------------------------------


def _load_jsonld_graph(spdx_dir, doc_name):
    """Load the @graph array from a JSON-LD file."""
    path = os.path.join(spdx_dir, f"{doc_name}-spdx3.jsonld")
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return json.load(f).get("@graph", [])


@pytest.fixture(scope="session")
def app_graph(spdx_dir, spdx_version):
    """Raw @graph from app SPDX 3.0 JSON-LD (empty list for 2.x)."""
    if not spdx_version.startswith("3"):
        return []
    return _load_jsonld_graph(spdx_dir, "app")


@pytest.fixture(scope="session")
def zephyr_graph(spdx_dir, spdx_version):
    """Raw @graph from zephyr SPDX 3.0 JSON-LD (empty list for 2.x)."""
    if not spdx_version.startswith("3"):
        return []
    return _load_jsonld_graph(spdx_dir, "zephyr")


@pytest.fixture(scope="session")
def build_graph(spdx_dir, spdx_version):
    """Raw @graph from build SPDX 3.0 JSON-LD (empty list for 2.x)."""
    if not spdx_version.startswith("3"):
        return []
    return _load_jsonld_graph(spdx_dir, "build")


@pytest.fixture(scope="session")
def modules_graph(spdx_dir, spdx_version):
    """Raw @graph from modules-deps SPDX 3.0 JSON-LD (empty list for 2.x)."""
    if not spdx_version.startswith("3"):
        return []
    return _load_jsonld_graph(spdx_dir, "modules-deps")
