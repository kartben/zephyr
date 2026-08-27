#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Tests for the build-free SBOM mode (``west spdx --modules-only``).

The dependency document is built from module metadata alone, so these tests
drive it from a fixture meta file and need neither a build nor a west
workspace. The build-dependent paths stay covered by the twister test at
tests/application_development/software_bill_of_materials.
"""

import os
import sys
import textwrap

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "pylib"))

from zspdx.sbom import SBOMConfig, make_spdx  # noqa: E402
from zspdx.version import parse  # noqa: E402

META = textwrap.dedent(
    """\
    zephyr:
      path: zephyr
      revision: 3853d52ce0000000000000000000000000000000
      remote: https://github.com/zephyrproject-rtos/zephyr
      tags:
        - v4.4.0
    modules:
      - name: mbedtls
        path: modules/crypto/mbedtls
        revision: c43f34b93797e81fd0257c30004dcd0ae332ae51
        remote: https://github.com/zephyrproject-rtos/mbedtls
        security:
          external-references:
            - cpe:2.3:a:trustedfirmware:mbed_tls:4.1.1:*:*:*:*:*:*:*
            - pkg:github/Mbed-TLS/mbedtls@v4.1.1
      - name: mcuboot
        path: bootloader/mcuboot
        revision: 7ad67106c3253d03ae4bd8ab48e6bdf7bc46f43e
        remote: https://github.com/zephyrproject-rtos/mcuboot
      - name: hal_espressif
        path: modules/hal/espressif
        revision: aabbccddeeff00112233445566778899aabbccdd
        remote: https://github.com/zephyrproject-rtos/hal_espressif
    workspace: {}
    """
)


def generate(tmp_path, meta=META, version="2.3"):
    """Run the modules-only walk over *meta*, returning the output directory."""
    meta_file = tmp_path / "zephyr.meta"
    meta_file.write_text(meta, encoding="utf-8")
    out = tmp_path / "spdx"
    out.mkdir()

    cfg = SBOMConfig()
    cfg.modules_only = True
    cfg.meta_file = str(meta_file)
    cfg.spdx_dir = str(out)
    cfg.spdx_version = parse(version)
    cfg.namespace_prefix = "http://spdx.org/spdxdocs/zephyr-test"

    assert make_spdx(cfg) is True
    return out


def test_only_the_dependency_document_is_written(tmp_path):
    """Without a build there are no sources, targets or SDK to describe."""
    out = generate(tmp_path)
    assert sorted(p.name for p in out.iterdir()) == ["modules-deps.spdx"]


def test_no_build_directory_is_required(tmp_path):
    """The whole point: none of the CMake state the full walk needs is touched."""
    out = generate(tmp_path)
    content = (out / "modules-deps.spdx").read_text(encoding="utf-8")
    assert "SPDXRef-mbedtls-deps" in content


def test_curated_security_references_are_emitted(tmp_path):
    content = (generate(tmp_path) / "modules-deps.spdx").read_text(encoding="utf-8")
    assert (
        "ExternalRef: SECURITY cpe23Type "
        "cpe:2.3:a:trustedfirmware:mbed_tls:4.1.1:*:*:*:*:*:*:*" in content
    )
    assert "ExternalRef: PACKAGE-MANAGER purl pkg:github/Mbed-TLS/mbedtls@v4.1.1" in content


def test_modules_without_metadata_get_an_scm_purl(tmp_path):
    """Every module carries a purl, even though only a curated CPE can match a CVE."""
    content = (generate(tmp_path) / "modules-deps.spdx").read_text(encoding="utf-8")
    assert (
        "purl pkg:github/zephyrproject-rtos/mcuboot@"
        "7ad67106c3253d03ae4bd8ab48e6bdf7bc46f43e" in content
    )


def test_every_manifest_module_is_described(tmp_path):
    """Dependency packages describe the manifest, not what a build consumed."""
    content = (generate(tmp_path) / "modules-deps.spdx").read_text(encoding="utf-8")
    for spdx_id in (
        "SPDXRef-zephyr-deps",
        "SPDXRef-mbedtls-deps",
        "SPDXRef-mcuboot-deps",
        # '_' is not allowed in an SPDX id, so it is normalised to '-'.
        "SPDXRef-hal-espressif-deps",
    ):
        assert f"SPDXID: {spdx_id}" in content


def test_relationships_to_absent_source_packages_are_dropped(tmp_path):
    """The "-sources" packages only exist when a build was walked.

    walk_relationships() skips relationships whose endpoints are missing, so the
    document must not reference them.
    """
    content = (generate(tmp_path) / "modules-deps.spdx").read_text(encoding="utf-8")
    assert "-sources" not in content
    assert "SPDXRef-mbedtls-deps DEPENDENCY_OF SPDXRef-zephyr-deps" in content


def test_spdx_2_2_is_supported(tmp_path):
    out = generate(tmp_path, version="2.2")
    content = (out / "modules-deps.spdx").read_text(encoding="utf-8")
    assert "SPDXVersion: SPDX-2.2" in content


@pytest.mark.parametrize(
    "meta",
    [
        "",
        "not: a mapping\n",
        "zephyr:\n  path: zephyr\n",  # no "modules" key
    ],
    ids=["empty", "wrong-shape", "no-modules"],
)
def test_a_malformed_meta_file_fails_cleanly(tmp_path, meta):
    """--meta takes an arbitrary path, so a bad file must not traceback."""
    meta_file = tmp_path / "zephyr.meta"
    meta_file.write_text(meta, encoding="utf-8")
    out = tmp_path / "spdx"
    out.mkdir()

    cfg = SBOMConfig()
    cfg.modules_only = True
    cfg.meta_file = str(meta_file)
    cfg.spdx_dir = str(out)
    cfg.namespace_prefix = "http://spdx.org/spdxdocs/zephyr-test"

    assert make_spdx(cfg) is False


def test_a_missing_meta_file_fails_cleanly(tmp_path):
    out = tmp_path / "spdx"
    out.mkdir()

    cfg = SBOMConfig()
    cfg.modules_only = True
    cfg.meta_file = str(tmp_path / "does-not-exist.meta")
    cfg.spdx_dir = str(out)
    cfg.namespace_prefix = "http://spdx.org/spdxdocs/zephyr-test"

    assert make_spdx(cfg) is False
