#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0
"""Tests for the identity the walker gives the SDK package.

The SDK is not a west project, so nothing in the manifest describes it. Everything
asserted about it is read from the installation itself, which is what these cover.
"""

import pytest
from conftest import NAMESPACE
from zspdx.walker import Walker, WalkerConfig

SDK_VERSION = "1.0.1"
SDK_PURL = "pkg:github/zephyrproject-rtos/sdk-ng@v1.0.1"
ZEPHYR_ORGANIZATION = "The Zephyr Project"


def _sdk_component(tmp_path, sdk_dir):
    """Run setup_sdk_component() against an SDK installation at ``sdk_dir``."""
    config = WalkerConfig()
    config.namespace_prefix = NAMESPACE
    config.build_dir = str(tmp_path)
    config.include_sdk = True

    walker = Walker(config)
    walker.setup_documents()
    walker.sdk_path = str(sdk_dir)
    walker.setup_sdk_component()

    return walker.component_sdk


@pytest.fixture
def installed_sdk(tmp_path):
    """An SDK installation carrying a version file, as a real one does."""
    sdk_dir = tmp_path / "zephyr-sdk"
    sdk_dir.mkdir()
    (sdk_dir / "sdk_version").write_text(f"{SDK_VERSION}\n")
    return sdk_dir


def test_version_comes_from_the_sdk_version_file(tmp_path, installed_sdk):
    """The SDK reports the version of the installation the headers came from."""
    component = _sdk_component(tmp_path, installed_sdk)
    assert component.version == SDK_VERSION


def test_supplier_is_the_zephyr_project(tmp_path, installed_sdk):
    """The SDK is a Zephyr Project release, whoever built the application."""
    component = _sdk_component(tmp_path, installed_sdk)
    assert component.supplier == ZEPHYR_ORGANIZATION


def test_purl_pins_the_matching_sdk_ng_release(tmp_path, installed_sdk):
    """The identifier points at the sdk-ng tag the installed version was cut from."""
    component = _sdk_component(tmp_path, installed_sdk)
    locators = [ref.locator for ref in component.external_references]
    assert SDK_PURL in locators


def test_download_location_pins_the_same_tag(tmp_path, installed_sdk):
    """URL and revision are what the serializers build a download location from."""
    component = _sdk_component(tmp_path, installed_sdk)
    assert component.url == "https://github.com/zephyrproject-rtos/sdk-ng"
    assert component.revision == f"v{SDK_VERSION}"


def test_nothing_is_asserted_without_a_version_file(tmp_path):
    """A toolchain that is not a Zephyr SDK gets no Zephyr identity attached."""
    sdk_dir = tmp_path / "other-toolchain"
    sdk_dir.mkdir()

    component = _sdk_component(tmp_path, sdk_dir)

    assert component.version == ""
    assert component.supplier == ""
    assert component.url == ""
    assert component.external_references == []
