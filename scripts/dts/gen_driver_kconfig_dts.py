#!/usr/bin/env python3
#
# Copyright (c) 2022 Kumar Gala <galak@kernel.org>
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import re

import yaml

try:
    # Use the C LibYAML parser if available, rather than the Python parser.
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader  # type: ignore


HEADER = """\
# Generated devicetree Kconfig
#
# SPDX-License-Identifier: Apache-2.0"""


KCONFIG_TEMPLATE = """
DT_COMPAT_{COMPAT} := {compat}

config DT_HAS_{COMPAT}_ENABLED
\tdef_bool $(dt_compat_enabled,$(DT_COMPAT_{COMPAT}))"""


# Character translation table used to derive Kconfig symbol names
TO_UNDERSCORES = str.maketrans("-,.@/+", "______")

# Top-level 'compatible:' key, i.e. at column 0
COMPATIBLE_LINE_RE = re.compile(r"^compatible:.*$", re.MULTILINE)

# First line that cannot belong to the value of a preceding top-level key
# (not indented, blank, a comment, or a zero-indented '- ' sequence entry)
NON_CONTINUATION_LINE_RE = re.compile(r"^(?![ \t]|#|-|$)", re.MULTILINE)

# Blank, comment and directive lines before the root node
LEADING_NOISE_RE = re.compile(r"^([ \t]*$|[ \t]*#|%)")

# Document start marker with no content on the same line
BARE_DOCUMENT_START_RE = re.compile(r"^---[ \t]*(#.*)?$")

# Plain, unquoted mapping key at the start of a line
PLAIN_KEY_RE = re.compile(r"^\w")

# Document start or end marker
DOCUMENT_MARKER_RE = re.compile(r"^(---|\.\.\.)", re.MULTILINE)

# Openers of constructs that can span multiple lines (flow collections,
# quoted scalars)
SPAN_OPENER_CHARS = frozenset("[{\"'")


def extract_compat(contents):
    # Extracts the value of the top-level 'compatible' key without parsing the
    # whole file. Returns the same string a full YAML parse would produce, or
    # None if a full parse is needed to decide.

    lines = contents.splitlines()

    # Only handle single-document files whose root node is a block mapping
    # with plain keys; anywhere else a column 0 'compatible:' line could be
    # (part of) data.
    index = 0
    seen_document_start = False
    while index < len(lines) and LEADING_NOISE_RE.match(lines[index]):
        index += 1
    if index < len(lines) and BARE_DOCUMENT_START_RE.match(lines[index]):
        seen_document_start = True
        index += 1
        while index < len(lines) and LEADING_NOISE_RE.match(lines[index]):
            index += 1
    if index >= len(lines) or not PLAIN_KEY_RE.match(lines[index]):
        return None

    markers = DOCUMENT_MARKER_RE.findall(contents)
    if len(markers) > (1 if seen_document_start else 0):
        return None

    match = COMPATIBLE_LINE_RE.search(contents)
    if not match:
        return None

    boundary = NON_CONTINUATION_LINE_RE.search(contents, match.end())
    end = boundary.start() if boundary else len(contents)

    before = contents[: match.start()]
    if SPAN_OPENER_CHARS.intersection(before) or "compatible" in before:
        # An earlier line could open a construct spanning the matched line
        # (making it data), or hold a differently spelled 'compatible' key
        # ('compatible : x', '"compatible": x', tagged, aliased, ...) which
        # must win over the matched one; any such spelling contains the word
        # or a quote. Compose the document prefix to resolve this exactly.
        try:
            root = yaml.compose(contents[:end], Loader=SafeLoader)
        except yaml.YAMLError:
            return None
        if not isinstance(root, yaml.MappingNode):
            return None
        for key, node in root.value:
            if key.value == "compatible" and isinstance(node, yaml.ScalarNode):
                return node.value
        return None

    # Parse the matched line plus its continuation lines, sliced from
    # 'contents' to preserve the exact line endings (block scalar chomping)
    chunk = contents[match.start() : end]

    try:
        data = yaml.load(chunk, Loader=SafeLoader)
    except yaml.YAMLError:
        return None

    if isinstance(data, dict) and isinstance(data.get("compatible"), str):
        return data["compatible"]

    return None


def binding_paths(bindings_dirs):
    # Yields paths to all bindings (.yaml files) in 'bindings_dirs'

    for bindings_dir in bindings_dirs:
        for root, _, filenames in os.walk(bindings_dir):
            for filename in filenames:
                if filename.endswith((".yaml", ".yml")):
                    yield os.path.join(root, filename)


def parse_args():
    # Returns parsed command-line arguments

    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument("--kconfig-out", required=True, help="path to write the Kconfig file")
    parser.add_argument(
        "--bindings-dirs",
        nargs='+',
        required=True,
        help="directory with bindings in YAML format, we allow multiple",
    )

    return parser.parse_args()


def main():
    args = parse_args()

    compats = set()

    for binding_path in binding_paths(args.bindings_dirs):
        with open(binding_path, encoding="utf-8") as f:
            contents = f.read()

        compat = extract_compat(contents)
        if compat is not None:
            compats.add(compat)
            continue

        if "compatible" not in contents and "\\" not in contents:
            # No 'compatible' spelled anywhere in the file, no need to parse
            # it. A backslash means a double-quoted scalar could still spell
            # it through escape sequences, e.g. "compat\x69ble".
            continue

        try:
            # Parsed PyYAML representation graph. For our purpose,
            # we don't need the whole file converted into a dict.
            root = yaml.compose(contents, Loader=SafeLoader)
        except yaml.YAMLError as e:
            print(
                f"WARNING: '{binding_path}' appears in binding "
                f"directories but isn't valid YAML: {e}"
            )
            continue

        if not isinstance(root, yaml.MappingNode):
            continue
        for key, node in root.value:
            if key.value == "compatible" and isinstance(node, yaml.ScalarNode):
                compats.add(node.value)
                break

    with open(args.kconfig_out, "w", encoding="utf-8") as kconfig_file:
        print(HEADER, file=kconfig_file)

        for c in sorted(compats):
            out = KCONFIG_TEMPLATE.format(compat=c, COMPAT=c.upper().translate(TO_UNDERSCORES))
            print(out, file=kconfig_file)


if __name__ == "__main__":
    main()
