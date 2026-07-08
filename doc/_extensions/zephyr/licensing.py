"""
Licensing exceptions page generator
====================================

This extension renders the :ref:`Zephyr_Licensing` page directly from the
machine-readable metadata that already lives in the repository's
``REUSE.toml`` file, so the page can never drift from the actual licensing of
the tree.

Rationale
*********

Zephyr as a whole is licensed under Apache-2.0, but it imports or reuses a
handful of components that use other licenses. Those *deviations* have to be
listed on the licensing page (see :ref:`external-contributions`). Maintaining
that list by hand is error prone: entries get duplicated, forgotten, or go
stale as files are added or removed.

Instead of a hand-written list, every deviation is described once, as a
standard `REUSE`_ annotation, and this extension turns those annotations into
the documentation page.

Metadata format
***************

The licensing/copyright facts stay 100% standard REUSE/SPDX. Each deviation is
a single ``[[annotations]]`` block in ``REUSE.toml`` that uses the standard
keys plus, at most, three small Zephyr-specific keys to *explain* the
deviation (the `REUSE`_ tool ignores unknown keys):

.. code-block:: toml

    [[annotations]]
    path = "subsys/net/lib/wireguard/crypto/**"
    SPDX-License-Identifier = "BSD-3-Clause"
    SPDX-FileCopyrightText = "Copyright (c) 2021 Daniel Hope (www.floorsense.nz)"
    SPDX-FileComment = "Only compiled into the firmware when the feature is enabled."
    Zephyr-Component = "WireGuard VPN"
    Zephyr-Origin = "wireguard-lwip <https://github.com/smartalock/wireguard-lwip>"
    Zephyr-Condition = "CONFIG_WIREGUARD"

Standard keys (the legally meaningful metadata):

``SPDX-License-Identifier``
    The license of the component. This is what is rendered as the component's
    license and is validated by the ``reuse`` tool.
``SPDX-FileCopyrightText``
    Copyright notice(s).
``SPDX-FileComment``
    Free-text rationale explaining *why* the deviation is acceptable (rendered
    as the "Impact" of the component). This is a standard SPDX tag.

Zephyr-specific keys (presentation only, all optional except ``Zephyr-Component``
which also acts as the marker that turns a plain REUSE annotation into a
documented licensing exception):

``Zephyr-Component``
    Human-friendly title for the component's section on the page.
``Zephyr-Origin``
    Upstream project the files come from, in ``name <url>`` form (the ``<url>``
    part is optional).
``Zephyr-Condition``
    Space/comma separated Kconfig option(s) that must be enabled for the files
    to end up in a build. Rendered with a :rst:role:`kconfig:option` cross
    reference.

The list of files for each component is expanded from the ``path`` globs
against the actual work tree, so it is always accurate and never needs to be
kept in sync by hand.

Usage
*****

Add the directive to a document (it takes no options)::

    .. zephyr-licensing-exceptions::

Copyright The Zephyr Project Contributors
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import re
import tomllib
from pathlib import Path
from typing import Any, Final

from docutils import nodes
from docutils.statemachine import StringList
from sphinx.application import Sphinx
from sphinx.util import logging
from sphinx.util.docutils import SphinxDirective

__version__ = "0.1.0"

ZEPHYR_BASE: Final[Path] = Path(__file__).parents[3]

logger = logging.getLogger(__name__)

# Licenses that are the project defaults and therefore not "deviations".
DEFAULT_LICENSES: Final[set[str]] = {"Apache-2.0", "CC-BY-4.0"}

# Marker key: a REUSE annotation is turned into a documented exception only if
# it carries this key.
MARKER_KEY: Final[str] = "Zephyr-Component"

# Matches the first `SPDX-License-Identifier:` tag near the top of a text file,
# used to sanity-check that the declared license matches the file itself.
_SPDX_RE: Final[re.Pattern] = re.compile(r"SPDX-License-Identifier:\s*(.+?)\s*(?:\*/|-->|#}|)\s*$")


def _spdx_url(license_expr: str) -> str | None:
    """Return the canonical SPDX page for a *simple* license id, else ``None``.

    License *expressions* (containing ``WITH``/``OR``/``AND`` or parentheses)
    have no single page, so they are rendered as plain text.
    """
    if re.fullmatch(r"[A-Za-z0-9.\-+]+", license_expr):
        return f"https://spdx.org/licenses/{license_expr}.html"
    return None


def _header_license(path: Path) -> str | None:
    """Best-effort read of the first in-file ``SPDX-License-Identifier`` tag.

    Only the first 40 lines are scanned (headers live at the top) which also
    avoids picking up unrelated matches in the body of e.g. Perl scripts that
    happen to mention the tag.
    """
    try:
        with path.open("r", encoding="utf-8", errors="ignore") as fp:
            for _, line in zip(range(40), fp):
                m = _SPDX_RE.search(line)
                if m:
                    return m.group(1).strip()
    except OSError:
        return None
    return None


class LicensingException:
    """A single documented licensing deviation, resolved against the tree."""

    def __init__(self, annotation: dict[str, Any]):
        self.title: str = annotation[MARKER_KEY]
        self.license: str = annotation.get("SPDX-License-Identifier", "")
        self.copyright: str = annotation.get("SPDX-FileCopyrightText", "")
        self.impact: str = annotation.get("SPDX-FileComment", "")
        self.origin: str = annotation.get("Zephyr-Origin", "")
        self.condition: str = annotation.get("Zephyr-Condition", "")

        patterns = annotation.get("path", [])
        if isinstance(patterns, str):
            patterns = [patterns]
        self.patterns: list[str] = patterns
        self.files: list[str] = self._resolve_files()

    def _resolve_files(self) -> list[str]:
        found: set[str] = set()
        for pattern in self.patterns:
            matches = [p for p in ZEPHYR_BASE.glob(pattern) if p.is_file()]
            if not matches:
                logger.warning(
                    "licensing: pattern '%s' for component '%s' matches no files",
                    pattern,
                    self.title,
                    type="licensing",
                )
            for match in matches:
                found.add(match.relative_to(ZEPHYR_BASE).as_posix())
        return sorted(found)

    def validate(self) -> None:
        """Warn if a file's own SPDX header contradicts the declared license."""
        for rel in self.files:
            header = _header_license(ZEPHYR_BASE / rel)
            if header and self.license and header != self.license:
                logger.warning(
                    "licensing: '%s' declares '%s' but the file header says '%s'",
                    rel,
                    self.license,
                    header,
                    type="licensing",
                )


class ZephyrLicensingExceptions(SphinxDirective):
    """Render the licensing exceptions table from ``REUSE.toml``."""

    has_content = False

    def _load_exceptions(self) -> list[LicensingException]:
        reuse_toml = ZEPHYR_BASE / "REUSE.toml"
        with reuse_toml.open("rb") as fp:
            data = tomllib.load(fp)

        exceptions = [
            LicensingException(ann)
            for ann in data.get("annotations", [])
            if MARKER_KEY in ann and ann.get("SPDX-License-Identifier") not in DEFAULT_LICENSES
        ]
        exceptions.sort(key=lambda e: e.title.lower())
        return exceptions

    def _body_rst(self, exc: LicensingException) -> list[str]:
        """reStructuredText for a component's body (everything but its title).

        Emitting RST for the body (rather than raw doctree nodes) lets the
        existing ``:zephyr_file:`` and ``:kconfig:option:`` roles do the heavy
        lifting, so the page reuses the theme's styling and needs no new CSS.
        """
        lines: list[str] = []

        # Metadata as a reST field list (rendered as a compact definition table
        # by the RTD theme, no custom CSS required).
        if exc.origin:
            lines.append(f":Origin: {_format_origin(exc.origin)}")

        url = _spdx_url(exc.license)
        license_field = f"`{exc.license} <{url}>`__" if url else f"``{exc.license}``"
        lines.append(f":License: {license_field}")

        if exc.condition:
            options = re.split(r"[,\s]+", exc.condition.strip())
            refs = ", ".join(f":kconfig:option:`{opt}`" for opt in options if opt)
            lines.append(f":Enabled by: {refs}")
        lines.append("")

        if exc.impact:
            lines.append(exc.impact)
            lines.append("")

        lines.append(f"Files ({len(exc.files)}):")
        lines.append("")
        for rel in exc.files:
            lines.append(f"* :zephyr_file:`{rel}`")
        lines.append("")

        return lines

    def _build_section(self, exc: LicensingException) -> nodes.section:
        section = nodes.section()
        section += nodes.title(text=exc.title)
        self.state.document.note_implicit_target(section, section)
        section["ids"] = [nodes.make_id(exc.title)]

        content = StringList(self._body_rst(exc), source="zephyr-licensing-exceptions")
        self.state.nested_parse(content, self.content_offset, section)
        return section

    def run(self) -> list[nodes.Node]:
        try:
            exceptions = self._load_exceptions()
        except (OSError, tomllib.TOMLDecodeError) as err:
            logger.warning("licensing: could not read REUSE.toml: %s", err, type="licensing")
            return [nodes.paragraph(text=f"Unable to generate licensing page: {err}")]

        if not exceptions:
            return [nodes.paragraph(text="No licensing exceptions are currently declared.")]

        result: list[nodes.Node] = []
        for exc in exceptions:
            exc.validate()
            result.append(self._build_section(exc))
        return result


def _format_origin(origin: str) -> str:
    """Turn ``name <url>`` into a reST link, or plain text if there is no URL."""
    m = re.match(r"\s*(.*?)\s*<(.+)>\s*$", origin)
    if m:
        name, url = m.group(1), m.group(2)
        return f"`{name} <{url}>`__"
    return origin


def setup(app: Sphinx) -> dict[str, Any]:
    app.add_directive("zephyr-licensing-exceptions", ZephyrLicensingExceptions)

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
