# Copyright (c) 2024 Basalte bv
# SPDX-License-Identifier: Apache-2.0

"""Verify structural content of generated SPDX documents."""

import re

import pytest


# ── SPDX tag-value parser ─────────────────────────────────────────


def parse_spdx(path):
    """Parse SPDX tag-value file into (tag, value) pairs."""
    pairs = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#") and ":" in line:
            tag, _, val = line.partition(":")
            pairs.append((tag.strip(), val.strip()))
    return pairs


def tag_values(pairs, tag):
    """Return all values for a given tag."""
    return [v for t, v in pairs if t == tag]


def tag_value(pairs, tag):
    """Return first value for a given tag, or None."""
    vals = tag_values(pairs, tag)
    return vals[0] if vals else None


def relationships(pairs, rln_type=None):
    """Return relationship strings, optionally filtered by type."""
    rlns = tag_values(pairs, "Relationship")
    if rln_type:
        rlns = [r for r in rlns if rln_type in r]
    return rlns


# ── Fixtures ───────────────────────────────────────────────────────

CORE_DOCS = ["app.spdx", "zephyr.spdx", "build.spdx", "modules-deps.spdx"]


@pytest.fixture(params=CORE_DOCS)
def doc(request, spdx_dir):
    """Parametrized fixture yielding parsed (tag, value) pairs for each core doc."""
    path = spdx_dir / request.param
    assert path.exists(), f"{request.param} was not generated"
    return parse_spdx(path)


@pytest.fixture
def app_doc(spdx_dir):
    return parse_spdx(spdx_dir / "app.spdx")


@pytest.fixture
def zephyr_doc(spdx_dir):
    return parse_spdx(spdx_dir / "zephyr.spdx")


@pytest.fixture
def build_doc(spdx_dir):
    return parse_spdx(spdx_dir / "build.spdx")


@pytest.fixture
def modules_deps_doc(spdx_dir):
    return parse_spdx(spdx_dir / "modules-deps.spdx")


@pytest.fixture
def sdk_doc(spdx_dir):
    path = spdx_dir / "sdk.spdx"
    if not path.exists():
        pytest.skip("sdk.spdx not generated (--include-sdk not used)")
    return parse_spdx(path)


# ── Common header checks (all core documents) ─────────────────────


class TestHeaders:
    """Every SPDX document must have valid header fields."""

    def test_version(self, doc, spdx_version):
        assert tag_value(doc, "SPDXVersion") == f"SPDX-{spdx_version}"

    def test_data_license(self, doc):
        assert tag_value(doc, "DataLicense") == "CC0-1.0"

    def test_spdx_id(self, doc):
        assert tag_value(doc, "SPDXID") == "SPDXRef-DOCUMENT"

    def test_has_name(self, doc):
        assert tag_value(doc, "DocumentName")

    def test_has_namespace(self, doc):
        assert tag_value(doc, "DocumentNamespace")

    def test_creator(self, doc):
        assert tag_value(doc, "Creator") == "Tool: Zephyr SPDX builder"

    def test_timestamp(self, doc):
        ts = tag_value(doc, "Created")
        assert ts and re.match(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z", ts)


# ── App document ──────────────────────────────────────────────────


class TestApp:
    def test_name(self, app_doc):
        assert tag_value(app_doc, "DocumentName") == "app-sources"

    def test_package(self, app_doc):
        assert "app-sources" in tag_values(app_doc, "PackageName")

    def test_describes(self, app_doc):
        assert any("SPDXRef-app-sources" in r for r in relationships(app_doc, "DESCRIBES"))

    def test_primary_purpose(self, app_doc, spdx_version):
        if spdx_version == "2.2":
            pytest.skip("PrimaryPackagePurpose not in SPDX 2.2")
        assert "SOURCE" in tag_values(app_doc, "PrimaryPackagePurpose")


# ── Zephyr document ──────────────────────────────────────────────


class TestZephyr:
    def test_name(self, zephyr_doc):
        assert tag_value(zephyr_doc, "DocumentName") == "zephyr-sources"

    def test_zephyr_package(self, zephyr_doc):
        assert "zephyr-sources" in tag_values(zephyr_doc, "PackageName")

    def test_describes(self, zephyr_doc):
        assert any("SPDXRef-zephyr-sources" in r for r in relationships(zephyr_doc, "DESCRIBES"))


# ── Build document ────────────────────────────────────────────────


class TestBuild:
    def test_name(self, build_doc):
        assert tag_value(build_doc, "DocumentName") == "build"

    def test_zephyr_final_package(self, build_doc):
        assert "zephyr_final" in tag_values(build_doc, "PackageName")

    def test_describes(self, build_doc):
        assert relationships(build_doc, "DESCRIBES")

    def test_external_document_refs(self, build_doc):
        assert tag_values(build_doc, "ExternalDocumentRef")


# ── Modules-deps document ────────────────────────────────────────


class TestModulesDeps:
    def test_name(self, modules_deps_doc):
        assert tag_value(modules_deps_doc, "DocumentName") == "modules-deps"

    def test_has_module_packages(self, modules_deps_doc):
        names = tag_values(modules_deps_doc, "PackageName")
        assert any(n.endswith("-deps") for n in names)

    def test_module_spdx_ids(self, modules_deps_doc):
        ids = tag_values(modules_deps_doc, "SPDXID")
        assert any(i.endswith("-deps") for i in ids)

    def test_describes(self, modules_deps_doc):
        assert relationships(modules_deps_doc, "DESCRIBES")

    def test_no_files_analyzed(self, modules_deps_doc):
        """Module deps packages are metadata-only, no files to analyze."""
        for v in tag_values(modules_deps_doc, "FilesAnalyzed"):
            assert v == "false"

    def test_external_refs_format(self, modules_deps_doc):
        """Any external references must be valid CPE 2.3 or PURL."""
        refs = tag_values(modules_deps_doc, "ExternalRef")
        for ref in refs:
            assert ref.startswith("SECURITY cpe23Type ") or ref.startswith(
                "PACKAGE-MANAGER purl "
            ), f"Unknown external reference format: {ref}"


# ── SDK document (optional, only with --include-sdk) ──────────────


class TestSDK:
    def test_name(self, sdk_doc):
        assert tag_value(sdk_doc, "DocumentName") == "sdk"

    def test_package(self, sdk_doc):
        assert "sdk" in tag_values(sdk_doc, "PackageName")

    def test_spdx_id(self, sdk_doc):
        assert "SPDXRef-sdk" in tag_values(sdk_doc, "SPDXID")

    def test_describes(self, sdk_doc):
        assert any("SPDXRef-sdk" in r for r in relationships(sdk_doc, "DESCRIBES"))
