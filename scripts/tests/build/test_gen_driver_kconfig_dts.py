#!/usr/bin/env python3
# Copyright (c) 2026 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for the fast 'compatible' extraction in gen_driver_kconfig_dts.py.

The extraction must be "right or abstain": for any input it either returns
exactly the string a full YAML parse of the file would find as the value of
the top-level 'compatible' key of the root mapping, or it returns None so
that the caller falls back to a full YAML parse.
"""

import os
import sys

import pytest
import yaml

sys.path.insert(0, os.path.join(os.environ["ZEPHYR_BASE"], "scripts", "dts"))
from gen_driver_kconfig_dts import extract_compat, main  # noqa: E402

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader


def compose_compat(contents):
    """Reference implementation: full YAML parse of the whole file.

    Returns the value of the top-level 'compatible' key of the root mapping,
    or None if the document has no such key, is not a mapping, or is not
    valid YAML.
    """
    try:
        root = yaml.compose(contents, Loader=SafeLoader)
    except yaml.YAMLError:
        return None
    if not isinstance(root, yaml.MappingNode):
        return None
    for key, node in root.value:
        if key.value == "compatible" and isinstance(node, yaml.ScalarNode):
            return node.value
    return None


# Bindings where the fast path is expected to find the compatible
EXTRACTABLE = [
    'compatible: "vnd,foo"\ndescription: x\n',
    'compatible: vnd,foo\n',
    "compatible: 'vnd,foo'\n",
    'compatible: vnd,foo # comment\n',
    'compatible: |\n  vnd,foo\n',
    'compatible: >\n  vnd,foo\n',
    'compatible: >-\n  vnd,foo\n',
    'compatible: |+\n  vnd,foo\n\n',
    'compatible: |2\n  vnd,foo\n',
    'compatible: |\n  vnd,foo',  # no final newline: affects chomping
    'compatible: vnd,\n  foo\n',  # multi-line plain scalar
    'compatible: >-\n  vnd,\n\n  foo\n',
    'compatible: vnd,foo\n# comment\ndescription: x\n',
    '---\ncompatible: vnd,foo\n',
    '# c\n---\n# c2\ncompatible: vnd,foo\n',
    'compatible: ""\n',
    'compatible: &a vnd,foo\n',
    'compatible: !!str vnd,foo\n',
    'description: |\n  compatible: vnd,fake\ncompatible: vnd,real\n',
    'description: |\n  text\n\ncompatible: vnd,foo\n',
    'compatible: vnd,first\ncompatible: vnd,second\n',
    '%YAML 1.1\n---\ncompatible: vnd,foo\n',
    '\n\n# comment\ncompatible: vnd,foo\n',
    'compatible: "vnd,devé"\n',
]

# Bindings where the fast path must abstain (return None); a full parse then
# produces the final result, whatever it is
ABSTAIN = [
    '"compatible": vnd,foo\n',  # quoted key
    'compatible : vnd,foo\n',  # space before colon
    '? compatible\n: vnd,foo\n',  # explicit key
    '{compatible: vnd,foo}\n',  # flow style root mapping
    '{\ncompatible: vnd,foo\n}\n',
    '|\ncompatible: vnd,foo\n',  # root node is a block scalar
    'just a scalar\n',
    '--- |\ncompatible: vnd,foo\n',
    'a: 1\n---\ncompatible: vnd,foo\n',  # multiple documents
    '---\nfoo: 1\n---\ncompatible: vnd,foo\n',
    'compatible: vnd,foo\n...\nmore: stuff\n',
    'compatible:\ndescription: x\n',  # null value
    'compatible: [vnd,foo]\n',  # not a scalar
    'compatible:\n  - vnd,foo\n',
    'compatible:\n- vnd,foo\n',
    'x: &a vnd,foo\ncompatible: *a\n',  # alias needs the whole document
    'x: &k compatible\n*k : vnd,foo\n',  # key spelled through an alias
    '"compat\\x69ble": vnd,foo\n',  # key spelled through escape sequences
    '"compat\\u0069ble": vnd,foo\n',
    'child-binding:\n  compatible: vnd,child\n',  # not top-level
    '',
    '# nothing here\n',
]

# Bindings whose handling legitimately differs between the C (libyaml) and
# pure Python loaders; only the right-or-abstain invariant applies
LOADER_DEPENDENT = [
    'compatible:\tvnd,foo\n',  # tab separation: rejected only by the pure Python loader
]


@pytest.mark.parametrize("contents", EXTRACTABLE)
def test_extracts_same_value_as_full_parse(contents):
    expected = compose_compat(contents)
    assert expected is not None, "test case is expected to have a compatible"
    assert extract_compat(contents) == expected


@pytest.mark.parametrize("contents", ABSTAIN)
def test_abstains_on_inputs_needing_full_parse(contents):
    assert extract_compat(contents) is None


@pytest.mark.parametrize("contents", EXTRACTABLE + ABSTAIN + LOADER_DEPENDENT)
def test_right_or_abstain(contents):
    # The overall invariant, regardless of how cases are categorized above
    fast = extract_compat(contents)
    assert fast is None or fast == compose_compat(contents)


def test_main_finds_compatibles_needing_full_parse(tmp_path, monkeypatch):
    # End-to-end check of the script, including the fallback to a full parse
    # for compatibles the fast path cannot see: keys spelled through escape
    # sequences or aliases contain no literal 'compatible' at column 0
    bindings = {
        "plain.yaml": 'compatible: vnd,plain\n',
        "escaped-key.yaml": '"compat\\x69ble": vnd,escaped\n',
        "alias-key.yaml": 'x: &k compatible\n*k : vnd,alias\n',
        "no-compat.yaml": 'description: nothing to see\n',
    }
    for filename, contents in bindings.items():
        (tmp_path / filename).write_text(contents, encoding="utf-8")

    kconfig_out = tmp_path / "Kconfig.dts"
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "gen_driver_kconfig_dts.py",
            "--kconfig-out",
            str(kconfig_out),
            "--bindings-dirs",
            str(tmp_path),
        ],
    )
    main()

    out = kconfig_out.read_text(encoding="utf-8")
    for compat in ("vnd,plain", "vnd,escaped", "vnd,alias"):
        assert f":= {compat}\n" in out


def test_bindings_in_tree():
    # extract_compat() must be right-or-abstain for every binding shipped in
    # the Zephyr tree, and is expected to succeed on the vast majority of
    # them (that is the point of having it)
    bindings_dir = os.path.join(os.environ["ZEPHYR_BASE"], "dts", "bindings")
    total = extracted = 0
    for root, _, filenames in os.walk(bindings_dir):
        for filename in filenames:
            if not filename.endswith((".yaml", ".yml")):
                continue
            with open(os.path.join(root, filename), encoding="utf-8") as f:
                contents = f.read()
            total += 1
            fast = extract_compat(contents)
            if fast is not None:
                extracted += 1
                assert fast == compose_compat(contents), filename
    assert total > 0
    assert extracted / total > 0.9
