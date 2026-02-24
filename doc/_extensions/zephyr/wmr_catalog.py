"""
WMR Catalog
============

Sphinx extension that renders a catalog of third-party Zephyr modules from
the `West Modules Registry <https://github.com/beriberikix/west-modules-registry>`_.

Usage
*****

.. code-block:: rst

   .. wmr-catalog::

Options
*******

- ``wmr_catalog_registry_url``: Remote URL of the registry repository.

Copyright (c) 2025 The Zephyr Project Contributors
SPDX-License-Identifier: Apache-2.0
"""

import glob as globmod
import os
from pathlib import Path
from typing import Any

from docutils import nodes
from docutils.parsers.rst import directives
from sphinx.application import Sphinx
from sphinx.util.docutils import SphinxDirective

from wmr_registry import WmrRegistry

__version__ = "0.1.0"


class WmrCatalog(SphinxDirective):
    """Render a card-based catalog of WMR modules."""

    option_spec = {
        "groups": directives.unchanged,
    }

    # ------------------------------------------------------------------
    # External doc discovery
    # ------------------------------------------------------------------

    def _find_external_docs(self):
        """Scan doc/develop/manifest/external/*.rst and return a set of
        normalised module names that have hand-written documentation."""

        src_dir = Path(self.env.srcdir)
        external_dir = src_dir / "develop" / "manifest" / "external"

        docs = {}
        if external_dir.is_dir():
            for rst in external_dir.glob("*.rst"):
                stem = rst.stem  # e.g. "emlearn", "memfault-firmware-sdk"
                # Normalise: lowercase, strip hyphens/underscores
                key = stem.lower().replace("-", "").replace("_", "")
                docs[key] = f"external/{stem}"  # relative RST path (no ext)

        return docs

    @staticmethod
    def _normalise(name):
        return name.lower().replace("-", "").replace("_", "")

    # ------------------------------------------------------------------
    # Node builders
    # ------------------------------------------------------------------

    def _build_tag(self, text):
        tag = nodes.inline(text=text, classes=["wmr-tag"])
        return tag

    def _build_card(self, name, meta, doc_ref):
        """Build the docutils node tree for a single module card."""

        card = nodes.container(classes=["wmr-card"])

        # -- header: name + tags ----------------------------------------
        header = nodes.container(classes=["wmr-card-header"])
        title = nodes.strong(text=name, classes=["wmr-card-title"])
        header += title

        groups = meta.get("groups", [])
        if groups:
            tags_wrap = nodes.container(classes=["wmr-tags"])
            for g in groups:
                tags_wrap += self._build_tag(g)
            header += tags_wrap

        card += header

        # -- description ------------------------------------------------
        desc = meta.get("description", "")
        if desc:
            card += nodes.paragraph(text=desc, classes=["wmr-description"])

        # -- links: homepage · repository · revision --------------------
        links_para = nodes.paragraph(classes=["wmr-links"])

        homepage = meta.get("homepage", "")
        url = meta.get("url", "")
        revision = meta.get("revision", "")

        parts = []
        if homepage:
            ref = nodes.reference("", "Homepage", refuri=homepage, internal=False)
            parts.append(ref)
        if url:
            display_url = url.rstrip(".git")
            ref = nodes.reference("", "Repository", refuri=display_url, internal=False)
            parts.append(ref)
        if revision:
            parts.append(nodes.literal(text=revision, classes=["wmr-revision"]))

        for i, part in enumerate(parts):
            if i > 0:
                links_para += nodes.Text(" \u00b7 ")
            links_para += part

        card += links_para

        # -- west.yml snippet -------------------------------------------
        snippet_text = meta.get("west-snippet", "")
        if snippet_text:
            snippet_wrapper = nodes.container(classes=["wmr-snippet"])
            snippet_label = nodes.paragraph(text="west.yml snippet:")
            snippet_label["classes"].append("wmr-snippet-label")
            snippet_wrapper += snippet_label

            # Build the full manifest wrapper around the raw snippet
            full_snippet = "manifest:\n  projects:\n"
            for line in snippet_text.strip().splitlines():
                full_snippet += f"    {line}\n"

            code = nodes.literal_block(
                full_snippet, full_snippet, language="yaml"
            )
            code["classes"].append("wmr-snippet-code")
            snippet_wrapper += code
            card += snippet_wrapper

        # -- link to detailed docs (if any) -----------------------------
        if doc_ref:
            doc_para = nodes.paragraph(classes=["wmr-docs-link"])
            ref_node = nodes.reference(
                "",
                "Detailed documentation \u2192",
                internal=True,
                refuri=doc_ref,
            )
            doc_para += ref_node
            card += doc_para

        return card

    # ------------------------------------------------------------------
    # Directive entry point
    # ------------------------------------------------------------------

    def run(self):
        # Resolve cache path
        cache_path = os.path.join(
            self.env.app.outdir, "_wmr_cache"
        )

        registry_url = self.env.config.wmr_catalog_registry_url
        registry = WmrRegistry(cache_path, registry_url)

        # Clone / update
        if not registry.clone_or_update():
            warning = nodes.warning()
            warning += nodes.paragraph(
                text="Could not fetch the West Modules Registry. "
                "Module catalog is unavailable."
            )
            return [warning]

        # Load modules
        modules = registry.get_latest_modules()

        if not modules:
            note = nodes.note()
            note += nodes.paragraph(
                text="No modules are currently registered in the "
                "West Modules Registry."
            )
            return [note]

        # Optional group filter
        group_filter = self.options.get("groups")
        if group_filter:
            required_groups = {
                g.strip() for g in group_filter.split(",") if g.strip()
            }
        else:
            required_groups = None

        # Discover existing external RST docs
        external_docs = self._find_external_docs()

        # Build catalog container
        catalog = nodes.container(classes=["wmr-catalog"])

        for name in sorted(modules.keys()):
            meta = modules[name]

            # Apply group filter
            if required_groups:
                module_groups = set(meta.get("groups", []))
                if not required_groups.issubset(module_groups):
                    continue

            # Check for matching external RST
            norm = self._normalise(name)
            doc_ref = external_docs.get(norm)

            card = self._build_card(name, meta, doc_ref)
            catalog += card

        return [catalog]


def setup(app: Sphinx) -> dict[str, Any]:
    app.add_config_value(
        "wmr_catalog_registry_url",
        "https://github.com/beriberikix/west-modules-registry",
        "env",
    )
    app.add_css_file("css/wmr_catalog.css")

    directives.register_directive("wmr-catalog", WmrCatalog)

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
