# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Pytest configuration for SPDX content validation tests."""

import os
import sys

import pytest
import yaml
from packaging import version
from spdx_tools.spdx.parser.parse_anything import parse_file


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
        help="Expected SPDX version (e.g., '2.2' or '2.3')",
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


def pytest_runtest_setup(item):
    """Skip tests based on min_spdx_version marker."""
    marker = item.get_closest_marker("min_spdx_version")
    if marker is not None:
        min_version = version.parse(marker.args[0])
        current_version = version.parse(item.config.getoption("--spdx-version"))
        if current_version < min_version:
            pytest.skip(f"Requires SPDX version >= {min_version}, got {current_version}")


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


@pytest.fixture(scope="session")
def app_doc(spdx_dir):
    """Fixture providing the parsed app.spdx document."""
    return parse_file(os.path.join(spdx_dir, "app.spdx"))


@pytest.fixture(scope="session")
def zephyr_doc(spdx_dir):
    """Fixture providing the parsed zephyr.spdx document."""
    return parse_file(os.path.join(spdx_dir, "zephyr.spdx"))


@pytest.fixture(scope="session")
def build_doc(spdx_dir):
    """Fixture providing the parsed build.spdx document."""
    return parse_file(os.path.join(spdx_dir, "build.spdx"))


@pytest.fixture(scope="session")
def modules_doc(spdx_dir):
    """Fixture providing the parsed modules-deps.spdx document."""
    return parse_file(os.path.join(spdx_dir, "modules-deps.spdx"))


@pytest.fixture(scope="session")
def zephyr_base():
    """Fixture providing the Zephyr source tree path."""
    base = os.environ.get("ZEPHYR_BASE")
    if not base:
        pytest.skip("ZEPHYR_BASE not set")
    return os.path.abspath(base)


@pytest.fixture(scope="session")
def sources_topdir(zephyr_base, zephyr_doc):
    """Fixture providing the directory zephyr.spdx file names are relative to.

    That is the west topdir, which the test has no direct handle on; look for the
    ancestor of ZEPHYR_BASE that the recorded file names resolve against.
    """
    names = [str(spdx_file.name).removeprefix("./") for spdx_file in zephyr_doc.files]
    candidate = zephyr_base
    while True:
        if any(os.path.isfile(os.path.join(candidate, name)) for name in names):
            return candidate
        parent = os.path.dirname(candidate)
        if parent == candidate:
            pytest.skip("cannot locate the directory zephyr.spdx file names are relative to")
        candidate = parent


@pytest.fixture(scope="session")
def maintainers(zephyr_base):
    """Fixture providing the parsed MAINTAINERS.yml of the Zephyr tree."""
    sys.path.insert(0, os.path.join(zephyr_base, "scripts"))
    try:
        from get_maintainer import Maintainers
    except ImportError as e:
        pytest.skip(f"cannot import get_maintainer: {e}")
    return Maintainers(os.path.join(zephyr_base, "MAINTAINERS.yml"))


@pytest.fixture(scope="session")
def zephyr_version():
    """Fixture providing the Zephyr version from the VERSION file."""
    zephyr_base = os.environ.get("ZEPHYR_BASE")
    if not zephyr_base:
        pytest.skip("ZEPHYR_BASE not set")

    version_file = os.path.join(zephyr_base, "VERSION")
    values = {}
    try:
        with open(version_file) as f:
            for line in f:
                key, sep, val = line.partition("=")
                if sep:
                    values[key.strip()] = val.strip()
    except OSError:
        pytest.skip(f"Cannot read {version_file}")

    try:
        return (
            f"{int(values['VERSION_MAJOR'])}"
            f".{int(values['VERSION_MINOR'])}"
            f".{int(values['PATCHLEVEL'])}"
        )
    except (KeyError, ValueError):
        pytest.skip(f"Cannot parse version from {version_file}")


@pytest.fixture(scope="session")
def zephyr_meta_remote(build_dir):
    """Fixture providing the zephyr SCM URL from zephyr.meta, if any."""
    meta_path = os.path.join(build_dir, "zephyr", "zephyr.meta")
    try:
        with open(meta_path) as f:
            content = yaml.safe_load(f)
    except OSError:
        return None

    zephyr = content.get("zephyr", {}) if content else {}
    return zephyr.get("remote") or zephyr.get("url")
