# Copyright (c) 2025 The Linux Foundation
# SPDX-License-Identifier: Apache-2.0

"""Tests for the doxylink Sphinx extension."""

import os
import tempfile
import zlib
from xml.etree.ElementTree import parse as ET_parse

import pytest

# Ensure the extensions directory is importable
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from doxylink import (
    build_tagfile,
    parse_inventory,
    sanitize_doxygen_name,
    write_tagfile,
)


def _make_inventory(entries_text: str) -> bytes:
    """Create a minimal Sphinx objects.inv v2 from entry text lines."""
    header = (
        b"# Sphinx inventory version 2\n"
        b"# Project: TestProject\n"
        b"# Version: 1.0\n"
        b"# The remainder of this file is compressed using zlib.\n"
    )
    compressed = zlib.compress(entries_text.encode("utf-8"))
    return header + compressed


class TestParseInventory:
    """Tests for the objects.inv parser."""

    def test_basic_doc_entry(self):
        data = _make_inventory(
            "develop/getting_started/index std:doc 1 develop/getting_started/index.html Getting Started\n"
        )
        entries = parse_inventory(data)
        assert len(entries) == 1
        assert entries[0]["name"] == "develop/getting_started/index"
        assert entries[0]["domain"] == "std"
        assert entries[0]["role"] == "doc"
        assert entries[0]["priority"] == 1
        assert entries[0]["uri"] == "develop/getting_started/index.html"
        assert entries[0]["dispname"] == "Getting Started"

    def test_basic_label_entry(self):
        data = _make_inventory(
            "getting_started std:label 1 develop/getting_started/index.html#getting-started Getting Started Guide\n"
        )
        entries = parse_inventory(data)
        assert len(entries) == 1
        assert entries[0]["name"] == "getting_started"
        assert entries[0]["uri"] == "develop/getting_started/index.html#getting-started"
        assert entries[0]["dispname"] == "Getting Started Guide"

    def test_dollar_replacement(self):
        data = _make_inventory("mypage std:doc 1 $.html -\n")
        entries = parse_inventory(data)
        assert entries[0]["uri"] == "mypage.html"
        assert entries[0]["dispname"] == "mypage"

    def test_dash_dispname(self):
        data = _make_inventory("some_label std:label 2 page.html#anchor -\n")
        entries = parse_inventory(data)
        assert entries[0]["dispname"] == "some_label"

    def test_hidden_entry(self):
        data = _make_inventory("hidden std:label -1 page.html#x Hidden Entry\n")
        entries = parse_inventory(data)
        assert len(entries) == 1
        assert entries[0]["priority"] == -1

    def test_multiple_entries(self):
        data = _make_inventory(
            "page1 std:doc 1 page1.html Page One\n"
            "page2 std:doc 1 page2.html Page Two\n"
            "label1 std:label 2 page1.html#s1 Label One\n"
            "some_func c:function 1 api.html#c.some_func some_func\n"
        )
        entries = parse_inventory(data)
        assert len(entries) == 4

    def test_invalid_version(self):
        data = b"# Sphinx inventory version 1\n# Project: X\n# Version: 1\n# zlib\n"
        data += zlib.compress(b"")
        with pytest.raises(ValueError, match="Unsupported inventory version"):
            parse_inventory(data)

    def test_empty_entries(self):
        data = _make_inventory("")
        entries = parse_inventory(data)
        assert len(entries) == 0


class TestSanitizeName:
    """Tests for the name sanitization function."""

    def test_simple_name(self):
        assert sanitize_doxygen_name("getting_started") == "getting_started"

    def test_hyphens(self):
        assert sanitize_doxygen_name("my-label") == "my_label"

    def test_slashes(self):
        assert sanitize_doxygen_name("develop/getting_started/index") == "develop_getting_started_index"

    def test_dots(self):
        assert sanitize_doxygen_name("config.option") == "config_option"

    def test_special_chars(self):
        assert sanitize_doxygen_name("label@#$%") == "label"

    def test_spaces(self):
        assert sanitize_doxygen_name("some label") == "some_label"


class TestBuildTagfile:
    """Tests for tagfile generation."""

    def test_doc_entries(self):
        entries = [
            {
                "name": "develop/getting_started/index",
                "domain": "std",
                "role": "doc",
                "priority": 1,
                "uri": "develop/getting_started/index.html",
                "dispname": "Getting Started",
            }
        ]
        root, count = build_tagfile(entries, ["std:doc"])
        assert count == 1

        compounds = root.findall("compound")
        assert len(compounds) == 1
        assert compounds[0].get("kind") == "page"
        assert compounds[0].find("name").text == "doc_develop_getting_started_index"
        assert compounds[0].find("title").text == "Getting Started"
        assert compounds[0].find("filename").text == "develop/getting_started/index"

    def test_label_entries(self):
        entries = [
            {
                "name": "getting_started",
                "domain": "std",
                "role": "label",
                "priority": 1,
                "uri": "develop/getting_started/index.html#getting-started",
                "dispname": "Getting Started Guide",
            }
        ]
        root, count = build_tagfile(entries, ["std:label"])
        assert count == 1

        compounds = root.findall("compound")
        assert compounds[0].find("name").text == "getting_started"
        assert compounds[0].find("title").text == "Getting Started Guide"
        # Fragment is stripped, .html is stripped (Doxygen adds it back)
        assert compounds[0].find("filename").text == "develop/getting_started/index"

    def test_hidden_entries_skipped(self):
        entries = [
            {
                "name": "hidden",
                "domain": "std",
                "role": "label",
                "priority": -1,
                "uri": "page.html#x",
                "dispname": "Hidden",
            }
        ]
        root, count = build_tagfile(entries, ["std:label"])
        assert count == 0

    def test_non_matching_roles_skipped(self):
        entries = [
            {
                "name": "some_func",
                "domain": "c",
                "role": "function",
                "priority": 1,
                "uri": "api.html#c.some_func",
                "dispname": "some_func",
            }
        ]
        root, count = build_tagfile(entries, ["std:doc", "std:label"])
        assert count == 0

    def test_duplicate_names_deduplicated(self):
        entries = [
            {
                "name": "intro",
                "domain": "std",
                "role": "label",
                "priority": 1,
                "uri": "page1.html#intro",
                "dispname": "Introduction",
            },
            {
                "name": "intro",
                "domain": "std",
                "role": "label",
                "priority": 2,
                "uri": "page2.html#intro",
                "dispname": "Another Intro",
            },
        ]
        root, count = build_tagfile(entries, ["std:label"])
        assert count == 1

    def test_mixed_roles(self):
        entries = [
            {
                "name": "index",
                "domain": "std",
                "role": "doc",
                "priority": 1,
                "uri": "index.html",
                "dispname": "Home",
            },
            {
                "name": "getting_started",
                "domain": "std",
                "role": "label",
                "priority": 1,
                "uri": "start.html#getting-started",
                "dispname": "Getting Started",
            },
        ]
        root, count = build_tagfile(entries, ["std:doc", "std:label"])
        assert count == 2


class TestWriteTagfile:
    """Tests for tagfile writing."""

    def test_write_and_read(self):
        entries = [
            {
                "name": "test_page",
                "domain": "std",
                "role": "label",
                "priority": 1,
                "uri": "test/page.html#section",
                "dispname": "Test Page",
            }
        ]
        root, count = build_tagfile(entries, ["std:label"])
        assert count == 1

        with tempfile.TemporaryDirectory() as tmpdir:
            tagfile_path = os.path.join(tmpdir, "subdir", "test.tag")
            write_tagfile(root, tagfile_path)

            assert os.path.exists(tagfile_path)

            tree = ET_parse(tagfile_path)
            tag_root = tree.getroot()
            assert tag_root.tag == "tagfile"

            compounds = tag_root.findall("compound")
            assert len(compounds) == 1
            assert compounds[0].find("name").text == "test_page"
            assert compounds[0].find("filename").text == "test/page"

    def test_write_empty_tagfile(self):
        from xml.etree.ElementTree import Element

        with tempfile.TemporaryDirectory() as tmpdir:
            tagfile_path = os.path.join(tmpdir, "empty.tag")
            write_tagfile(Element("tagfile"), tagfile_path)

            assert os.path.exists(tagfile_path)

            tree = ET_parse(tagfile_path)
            assert tree.getroot().tag == "tagfile"
            assert len(tree.getroot().findall("compound")) == 0


class TestEndToEnd:
    """End-to-end tests combining parsing and tagfile generation."""

    def test_full_pipeline(self):
        inv_data = _make_inventory(
            "develop/getting_started/index std:doc 1 "
            "develop/getting_started/index.html Getting Started\n"
            "getting_started std:label 1 "
            "develop/getting_started/index.html#getting-started Getting Started Guide\n"
            "bluetooth_overview std:label 1 "
            "connectivity/bluetooth/overview.html#bluetooth-overview Bluetooth Overview\n"
            "hidden_thing std:label -1 hidden.html#x Hidden\n"
            "k_thread_create c:function 1 api.html#c.k_thread_create k_thread_create\n"
        )

        entries = parse_inventory(inv_data)
        assert len(entries) == 5

        root, count = build_tagfile(entries, ["std:doc", "std:label"])
        assert count == 3  # 1 doc + 2 labels (hidden skipped, c:function skipped)

        with tempfile.TemporaryDirectory() as tmpdir:
            tagfile_path = os.path.join(tmpdir, "test.tag")
            write_tagfile(root, tagfile_path)

            tree = ET_parse(tagfile_path)
            compounds = tree.getroot().findall("compound")
            assert len(compounds) == 3

            names = {c.find("name").text for c in compounds}
            assert "doc_develop_getting_started_index" in names
            assert "getting_started" in names
            assert "bluetooth_overview" in names
