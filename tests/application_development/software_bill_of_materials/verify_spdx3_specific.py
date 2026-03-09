#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""
SPDX 3.0-specific content validation tests.

Tests for features unique to SPDX 3.0 that are not present in SPDX 2.x:
Build profile, Tool elements, LifecycleScopedRelationship, build configuration,
URI-based cross-document references, and license relationships.

These tests operate on raw JSON-LD @graph arrays rather than the adapter layer,
since they validate SPDX 3.0-specific structure.
"""

import pytest


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def elements_by_type(graph, element_type):
    """Return all elements from @graph matching a given type."""
    return [e for e in graph if e.get("type") == element_type]


def find_element(graph, element_type, **kwargs):
    """Find first element matching type and all keyword filters."""
    for e in graph:
        if e.get("type") != element_type:
            continue
        if all(e.get(k) == v for k, v in kwargs.items()):
            return e
    return None


def relationships_by_type(graph, rel_type):
    """Return all Relationship/LifecycleScopedRelationship elements of a given type."""
    return [
        e
        for e in graph
        if e.get("type") in ("Relationship", "LifecycleScopedRelationship")
        and e.get("relationshipType") == rel_type
    ]


# ---------------------------------------------------------------------------
# Build profile
# ---------------------------------------------------------------------------


class TestBuildProfile:
    """Tests for SPDX 3.0 Build profile elements."""

    def test_build_document_has_build_profile(self, build_graph):
        """Test that build document declares build profile conformance."""
        docs = elements_by_type(build_graph, "SpdxDocument")
        assert len(docs) == 1, "Expected exactly one SpdxDocument in build graph"
        conformance = docs[0].get("profileConformance", [])
        assert "build" in conformance, (
            f"build: SpdxDocument profileConformance {conformance} does not include 'build'"
        )

    def test_build_object_exists(self, build_graph):
        """Test that a build_Build element exists."""
        builds = elements_by_type(build_graph, "build_Build")
        assert len(builds) >= 1, "build: no build_Build element found"

    def test_build_has_build_type(self, build_graph):
        """Test that Build has a buildType URI."""
        builds = elements_by_type(build_graph, "build_Build")
        for b in builds:
            build_type = b.get("build_buildType", "")
            assert build_type.startswith("http"), (
                f"build: build_buildType '{build_type}' is not a valid URI"
            )

    def test_build_has_parameters(self, build_graph):
        """Test that Build has build_parameter entries."""
        builds = elements_by_type(build_graph, "build_Build")
        for b in builds:
            params = b.get("build_parameter", [])
            assert len(params) > 0, "build: build_Build has no build_parameter entries"

    def test_build_parameters_have_cmake_info(self, build_graph):
        """Test that build parameters include CMake configuration."""
        builds = elements_by_type(build_graph, "build_Build")
        assert len(builds) >= 1
        params = builds[0].get("build_parameter", [])
        keys = {p.get("key") for p in params}
        assert "cmake:version" in keys, (
            f"build: build_parameter keys {keys} missing 'cmake:version'"
        )

    def test_build_has_build_id(self, build_graph):
        """Test that Build has a buildId."""
        builds = elements_by_type(build_graph, "build_Build")
        for b in builds:
            assert b.get("build_buildId"), "build: build_Build has no build_buildId"


# ---------------------------------------------------------------------------
# Tool elements
# ---------------------------------------------------------------------------


class TestToolElements:
    """Tests for SPDX 3.0 Tool elements."""

    def test_tools_exist(self, build_graph):
        """Test that Tool elements exist in build document."""
        tools = elements_by_type(build_graph, "Tool")
        assert len(tools) >= 1, "build: no Tool elements found"

    def test_tool_has_name(self, build_graph):
        """Test that all Tool elements have a name."""
        tools = elements_by_type(build_graph, "Tool")
        for t in tools:
            assert t.get("name"), f"build: Tool '{t.get('spdxId')}' has no name"

    def test_compiler_tool_exists(self, build_graph):
        """Test that a compiler Tool element exists."""
        tools = elements_by_type(build_graph, "Tool")
        compiler_tools = [t for t in tools if "compiler" in t.get("spdxId", "").lower()]
        assert len(compiler_tools) >= 1, "build: no compiler Tool element found"

    def test_cmake_tool_exists(self, build_graph):
        """Test that a CMake Tool element exists."""
        tools = elements_by_type(build_graph, "Tool")
        cmake_tools = [t for t in tools if "cmake" in t.get("spdxId", "").lower()]
        assert len(cmake_tools) >= 1, "build: no CMake Tool element found"


# ---------------------------------------------------------------------------
# LifecycleScopedRelationship
# ---------------------------------------------------------------------------


class TestLifecycleScopedRelationships:
    """Tests for LifecycleScopedRelationship elements."""

    def test_lifecycle_relationships_exist(self, build_graph):
        """Test that LifecycleScopedRelationship elements exist."""
        lcrels = elements_by_type(build_graph, "LifecycleScopedRelationship")
        assert len(lcrels) > 0, "build: no LifecycleScopedRelationship elements found"

    def test_lifecycle_relationships_have_scope(self, build_graph):
        """Test that all LifecycleScopedRelationships have a scope."""
        lcrels = elements_by_type(build_graph, "LifecycleScopedRelationship")
        for rel in lcrels:
            assert rel.get("scope"), (
                f"build: LifecycleScopedRelationship '{rel.get('spdxId')}' has no scope"
            )

    def test_uses_tool_relationships_exist(self, build_graph):
        """Test that usesTool relationships exist."""
        lcrels = elements_by_type(build_graph, "LifecycleScopedRelationship")
        uses_tool = [r for r in lcrels if r.get("relationshipType") == "usesTool"]
        assert len(uses_tool) > 0, "build: no usesTool LifecycleScopedRelationship found"

    def test_uses_tool_references_tool_elements(self, build_graph):
        """Test that usesTool relationships reference actual Tool elements."""
        tool_ids = {
            t.get("spdxId") for t in elements_by_type(build_graph, "Tool")
        }
        lcrels = elements_by_type(build_graph, "LifecycleScopedRelationship")
        uses_tool = [r for r in lcrels if r.get("relationshipType") == "usesTool"]

        for rel in uses_tool:
            to_ids = rel.get("to", [])
            tool_refs = [tid for tid in to_ids if tid in tool_ids]
            assert len(tool_refs) > 0, (
                f"build: usesTool rel '{rel.get('spdxId')}' does not reference any Tool, "
                f"to={to_ids}"
            )


# ---------------------------------------------------------------------------
# Build configuration elements
# ---------------------------------------------------------------------------


class TestBuildConfiguration:
    """Tests for build configuration elements (compile flags, link flags, etc.)."""

    def test_build_config_elements_exist(self, build_graph):
        """Test that build-config software_File elements exist."""
        configs = [
            e
            for e in build_graph
            if e.get("type") == "software_File"
            and "build-config" in e.get("spdxId", "")
        ]
        assert len(configs) > 0, "build: no build-config software_File elements found"

    def test_build_config_has_purpose_configuration(self, build_graph):
        """Test that build-config files have primaryPurpose 'configuration'."""
        configs = [
            e
            for e in build_graph
            if e.get("type") == "software_File"
            and "build-config" in e.get("spdxId", "")
        ]
        for c in configs:
            assert c.get("software_primaryPurpose") == "configuration", (
                f"build: build-config '{c.get('spdxId')}' purpose is "
                f"'{c.get('software_primaryPurpose')}', expected 'configuration'"
            )

    def test_configures_relationships_exist(self, build_graph):
        """Test that 'configures' relationships exist linking configs to artifacts."""
        configures_rels = relationships_by_type(build_graph, "configures")
        assert len(configures_rels) > 0, "build: no 'configures' relationships found"


# ---------------------------------------------------------------------------
# Cross-document URI references
# ---------------------------------------------------------------------------


class TestCrossDocumentURIs:
    """Tests for SPDX 3.0 URI-based cross-document references."""

    def test_build_references_app_elements(self, build_graph, app_graph):
        """Verify that build doc relationships reference elements from app doc."""
        app_ids = {e.get("spdxId") for e in app_graph if e.get("spdxId")}
        all_rels = [
            e
            for e in build_graph
            if e.get("type") in ("Relationship", "LifecycleScopedRelationship")
        ]

        cross_refs = set()
        for rel in all_rels:
            from_id = rel.get("from", "")
            if from_id in app_ids:
                cross_refs.add(from_id)
            for to_id in rel.get("to", []):
                if to_id in app_ids:
                    cross_refs.add(to_id)

        assert len(cross_refs) > 0, (
            "build: no relationships reference elements from app document"
        )

    def test_build_references_zephyr_elements(self, build_graph, zephyr_graph):
        """Verify that build doc relationships reference elements from zephyr doc."""
        zephyr_ids = {e.get("spdxId") for e in zephyr_graph if e.get("spdxId")}
        all_rels = [
            e
            for e in build_graph
            if e.get("type") in ("Relationship", "LifecycleScopedRelationship")
        ]

        cross_refs = set()
        for rel in all_rels:
            from_id = rel.get("from", "")
            if from_id in zephyr_ids:
                cross_refs.add(from_id)
            for to_id in rel.get("to", []):
                if to_id in zephyr_ids:
                    cross_refs.add(to_id)

        assert len(cross_refs) > 0, (
            "build: no relationships reference elements from zephyr document"
        )

    def test_uri_based_ids_are_consistent(self, build_graph):
        """Test that all element IDs use the namespace prefix pattern."""
        for elem in build_graph:
            sid = elem.get("spdxId", elem.get("@id", ""))
            if not sid:
                continue
            assert ":" in sid or sid.startswith("http"), (
                f"Element ID '{sid}' does not look like a valid URI"
            )


# ---------------------------------------------------------------------------
# License relationships
# ---------------------------------------------------------------------------


class TestLicenseRelationships:
    """Tests for SPDX 3.0 license expression via relationships."""

    def test_files_have_declared_license_relationships(self, app_graph):
        """Test that files have hasDeclaredLicense relationships."""
        file_ids = {
            e.get("spdxId")
            for e in app_graph
            if e.get("type") == "software_File"
        }
        declared_rels = relationships_by_type(app_graph, "hasDeclaredLicense")
        licensed_file_ids = {
            r.get("from") for r in declared_rels if r.get("from") in file_ids
        }

        assert len(licensed_file_ids) > 0, (
            "app: no files have hasDeclaredLicense relationships"
        )

    def test_license_expressions_exist(self, app_graph):
        """Test that simplelicensing_LicenseExpression elements exist."""
        lic_exprs = elements_by_type(app_graph, "simplelicensing_LicenseExpression")
        assert len(lic_exprs) > 0, (
            "app: no simplelicensing_LicenseExpression elements found"
        )

    def test_license_expression_has_value(self, app_graph):
        """Test that license expressions have the expression string."""
        lic_exprs = elements_by_type(app_graph, "simplelicensing_LicenseExpression")
        for le in lic_exprs:
            expr = le.get("simplelicensing_licenseExpression", "")
            assert expr, (
                f"app: LicenseExpression '{le.get('spdxId')}' has empty expression"
            )

    def test_apache_license_present(self, app_graph):
        """Test that Apache-2.0 license expression exists."""
        lic_exprs = elements_by_type(app_graph, "simplelicensing_LicenseExpression")
        apache_exprs = [
            le
            for le in lic_exprs
            if "Apache-2.0" in le.get("simplelicensing_licenseExpression", "")
        ]
        assert len(apache_exprs) > 0, "app: no Apache-2.0 license expression found"


# ---------------------------------------------------------------------------
# Document structure
# ---------------------------------------------------------------------------


class TestDocumentStructure:
    """Tests for SPDX 3.0 document structure and metadata."""

    def test_creation_info_has_spec_version(self, app_graph):
        """Test that CreationInfo has specVersion."""
        ci_elems = elements_by_type(app_graph, "CreationInfo")
        # CreationInfo may also be inlined in SpdxDocument
        docs = elements_by_type(app_graph, "SpdxDocument")
        for doc in docs:
            ci = doc.get("creationInfo", {})
            if isinstance(ci, dict):
                assert ci.get("specVersion", "").startswith("3.0"), (
                    f"app: CreationInfo specVersion is '{ci.get('specVersion')}'"
                )
                return
        # Fallback: check standalone CreationInfo
        assert len(ci_elems) >= 1, "app: no CreationInfo element found"
        for ci in ci_elems:
            assert ci.get("specVersion", "").startswith("3.0"), (
                f"app: CreationInfo specVersion is '{ci.get('specVersion')}'"
            )

    def test_document_has_namespace_map(self, app_graph):
        """Test that SpdxDocument has namespaceMap entries."""
        docs = elements_by_type(app_graph, "SpdxDocument")
        assert len(docs) == 1
        ns_maps = docs[0].get("namespaceMap", [])
        assert len(ns_maps) > 0, "app: SpdxDocument has no namespaceMap entries"

    def test_document_has_root_element(self, app_graph):
        """Test that SpdxDocument has rootElement."""
        docs = elements_by_type(app_graph, "SpdxDocument")
        assert len(docs) == 1
        root_elems = docs[0].get("rootElement", [])
        assert len(root_elems) > 0, "app: SpdxDocument has no rootElement"

    def test_document_has_data_license(self, app_graph):
        """Test that SpdxDocument has dataLicense."""
        docs = elements_by_type(app_graph, "SpdxDocument")
        assert len(docs) == 1
        data_license = docs[0].get("dataLicense", "")
        assert data_license, "app: SpdxDocument has no dataLicense"
        assert "CC0-1.0" in data_license, (
            f"app: dataLicense '{data_license}' does not reference CC0-1.0"
        )

    def test_software_agent_exists(self, app_graph):
        """Test that a SoftwareAgent element exists."""
        agents = elements_by_type(app_graph, "SoftwareAgent")
        assert len(agents) >= 1, "app: no SoftwareAgent element found"
