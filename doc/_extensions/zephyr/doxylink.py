"""
Doxylink Sphinx Extension
#########################

Copyright (c) 2025 The Linux Foundation
SPDX-License-Identifier: Apache-2.0

Introduction
============

This Sphinx extension generates Doxygen-compatible tagfiles from Sphinx
``objects.inv`` inventory files. This enables cross-referencing from Doxygen
API documentation to Sphinx narrative documentation using standard Doxygen
``@ref`` syntax.

The extension runs during the ``builder-inited`` phase, before
:py:mod:`zephyr.doxyrunner` executes Doxygen, ensuring the tagfile is
available when Doxygen processes its input.

Usage in Doxygen comments
=========================

Using ``@ref`` with labels from the Sphinx inventory::

    /**
     * See @ref getting_started for setup instructions.
     * See also @ref bluetooth_overview "Bluetooth Overview".
     */

Using ``\\sphinxref`` alias for direct links (supports anchors)::

    /**
     * \\sphinxref{develop/getting_started/index.html,Getting Started Guide}
     */

Configuration options
=====================

- ``doxylink_inventories``: Dictionary mapping project names to config dicts.
  Each config dict supports:

  - ``url`` (str): URL to fetch ``objects.inv`` from.
  - ``local`` (str): Local path to ``objects.inv`` (fallback).
  - ``tagfile`` (str): Output path for the generated Doxygen tagfile.

- ``doxylink_roles`` (list[str]): Sphinx inventory roles to include.
  Default: ``["std:doc", "std:label"]``
"""

import os
import re
import urllib.request
import zlib
from typing import Any
from xml.etree.ElementTree import Element, ElementTree, SubElement, indent

from sphinx.application import Sphinx
from sphinx.util import logging

__version__ = "0.1.0"

logger = logging.getLogger(__name__)

DEFAULT_ROLES = ["std:doc", "std:label"]


def parse_inventory(data: bytes) -> list[dict]:
    """Parse a Sphinx objects.inv version 2 file.

    The inventory format consists of a 4-line text header followed by
    zlib-compressed entry lines. Each entry has the format::

        name domain:role priority uri dispname

    Args:
        data: Raw bytes of the objects.inv file.

    Returns:
        List of entry dicts with keys: name, domain, role, priority, uri,
        dispname.
    """

    # Read the 4-line header
    pos = 0
    header_lines = []
    for _ in range(4):
        nl = data.index(b"\n", pos)
        header_lines.append(data[pos:nl].decode("utf-8"))
        pos = nl + 1

    if "Sphinx inventory version 2" not in header_lines[0]:
        raise ValueError(f"Unsupported inventory version: {header_lines[0]}")

    # Decompress the entry data
    decompressed = zlib.decompress(data[pos:]).decode("utf-8")

    entries = []
    entry_re = re.compile(r"(.+?)\s+(\w+):(\w+)\s+(-?\d+)\s+(\S*)\s+(.*)")

    for line in decompressed.splitlines():
        m = entry_re.match(line)
        if not m:
            continue

        name, domain, role, priority, uri, dispname = m.groups()

        if dispname == "-":
            dispname = name

        # The ``$`` placeholder in URIs is replaced with the entry name
        uri = uri.replace("$", name)

        entries.append(
            {
                "name": name,
                "domain": domain,
                "role": role,
                "priority": int(priority),
                "uri": uri,
                "dispname": dispname,
            }
        )

    return entries


def fetch_inventory_from_url(url: str) -> bytes | None:
    """Fetch an objects.inv file from a URL.

    Args:
        url: URL to the objects.inv file.

    Returns:
        Raw bytes of the inventory, or None on failure.
    """

    try:
        logger.info(f"[doxylink] Fetching inventory from {url}")
        req = urllib.request.Request(url, headers={"User-Agent": "Zephyr-Doxylink/1.0"})
        with urllib.request.urlopen(req, timeout=30) as resp:
            return resp.read()
    except Exception as e:
        logger.warning(f"[doxylink] Failed to fetch inventory from {url}: {e}")
        return None


def read_inventory_from_file(path: str) -> bytes | None:
    """Read an objects.inv file from the local filesystem.

    Args:
        path: Path to the objects.inv file.

    Returns:
        Raw bytes of the inventory, or None on failure.
    """

    try:
        with open(path, "rb") as f:
            return f.read()
    except Exception as e:
        logger.warning(f"[doxylink] Failed to read inventory from {path}: {e}")
        return None


def sanitize_doxygen_name(name: str) -> str:
    """Sanitize a name for use as a Doxygen page reference.

    Doxygen page identifiers should be valid C-like identifiers
    (alphanumeric characters and underscores).

    Args:
        name: Original inventory entry name.

    Returns:
        Sanitized name suitable for use with Doxygen ``@ref``.
    """

    sanitized = re.sub(r"[/\-. ]", "_", name)
    sanitized = re.sub(r"[^a-zA-Z0-9_]", "", sanitized)
    return sanitized


def build_tagfile(entries: list[dict], roles: list[str]) -> tuple[Element, int]:
    """Build a Doxygen tagfile XML tree from inventory entries.

    For ``std:doc`` entries, page names are prefixed with ``doc_`` to
    distinguish them from label-based entries. For ``std:label`` entries,
    the RST label name is used directly (after sanitization).

    Each entry becomes a ``<compound kind="page">`` element. Doxygen
    appends ``.html`` to the ``<filename>`` for page compounds, so the
    extension stored here should not contain ``.html``.

    Args:
        entries: Parsed inventory entries.
        roles: List of ``"domain:role"`` strings to include.

    Returns:
        Tuple of (XML root element, count of entries written).
    """

    root = Element("tagfile", attrib={"doxygen_version": "sphinx-inventory"})

    seen_names = set()
    count = 0

    for entry in entries:
        domain_role = f"{entry['domain']}:{entry['role']}"
        if domain_role not in roles:
            continue

        # Skip hidden entries (negative priority)
        if entry["priority"] < 0:
            continue

        # Generate the Doxygen reference name
        if domain_role == "std:doc":
            ref_name = "doc_" + sanitize_doxygen_name(entry["name"])
        else:
            ref_name = sanitize_doxygen_name(entry["name"])

        if ref_name in seen_names:
            continue
        seen_names.add(ref_name)

        # Extract the base page URI (strip fragment and .html)
        uri = entry["uri"]
        if "#" in uri:
            page_uri = uri.split("#")[0]
        else:
            page_uri = uri

        # Doxygen appends .html for page compounds, so strip it here
        if page_uri.endswith(".html"):
            page_uri = page_uri[:-5]
        page_uri = page_uri.rstrip("/")

        if not page_uri:
            continue

        compound = SubElement(root, "compound", kind="page")
        SubElement(compound, "name").text = ref_name
        SubElement(compound, "title").text = entry["dispname"]
        SubElement(compound, "filename").text = page_uri
        count += 1

    return root, count


def write_tagfile(root: Element, path: str) -> None:
    """Write a Doxygen tagfile XML tree to disk.

    Creates parent directories as needed.

    Args:
        root: XML root element.
        path: Output file path.
    """

    tree = ElementTree(root)
    indent(tree, space="  ")

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tree.write(path, encoding="unicode", xml_declaration=True)


def generate_tagfiles(app: Sphinx) -> None:
    """Process configured inventories and generate Doxygen tagfiles.

    This callback runs during ``builder-inited`` with a low priority number
    (high priority in execution order) to ensure it completes before
    :py:mod:`zephyr.doxyrunner` launches Doxygen.
    """

    inventories = app.config.doxylink_inventories
    if not inventories:
        return

    roles = app.config.doxylink_roles or DEFAULT_ROLES

    for name, config in inventories.items():
        tagfile_path = config.get("tagfile")
        if not tagfile_path:
            logger.warning(f"[doxylink] No tagfile path for project '{name}', skipping")
            continue

        # Try to obtain inventory data from configured sources
        data = None

        url = config.get("url")
        if url:
            data = fetch_inventory_from_url(url)

        if data is None:
            local = config.get("local")
            if local:
                data = read_inventory_from_file(local)

        if data is None:
            logger.warning(
                f"[doxylink] Could not obtain inventory for '{name}', "
                f"writing empty tagfile at {tagfile_path}"
            )
            write_tagfile(Element("tagfile"), tagfile_path)
            continue

        try:
            entries = parse_inventory(data)
            root, count = build_tagfile(entries, roles)
            write_tagfile(root, tagfile_path)
            logger.info(
                f"[doxylink] Generated tagfile for '{name}' with {count} entries "
                f"at {tagfile_path}"
            )
        except Exception as e:
            logger.warning(f"[doxylink] Failed to process inventory for '{name}': {e}")
            # Write empty tagfile so Doxygen does not fail
            write_tagfile(Element("tagfile"), tagfile_path)


def setup(app: Sphinx) -> dict[str, Any]:
    app.add_config_value("doxylink_inventories", {}, "env")
    app.add_config_value("doxylink_roles", DEFAULT_ROLES, "env")

    # Use low priority number to run before doxyrunner (default priority 500)
    app.connect("builder-inited", generate_tagfiles, priority=100)

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
