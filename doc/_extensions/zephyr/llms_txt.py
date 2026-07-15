"""
llms.txt support
################

Copyright (c) 2026 The Linux Foundation
SPDX-License-Identifier: Apache-2.0

Introduction
============

This extension integrates the ``sphinx_llm.txt`` extension (from the
`sphinx-llm <https://github.com/NVIDIA/sphinx-llm>`_ package) with Zephyr's
documentation build. When enabled, the HTML build additionally produces
LLM-friendly markdown output following the `llms.txt <https://llmstxt.org>`_
convention:

- ``llms.txt``: a markdown "sitemap" listing all documentation pages.
- ``llms-full.txt``: the entire documentation as a single markdown file.
- A markdown rendition of every page, next to its HTML version (e.g.
  ``develop/getting_started/index.html.md``).

``sphinx_llm.txt`` works by spawning a second Sphinx build using the
``markdown`` builder (from ``sphinx-markdown-builder``) and merging its output
into the HTML output directory. As-is, this sub-build is incompatible with
Zephyr's documentation build, hence this wrapper, which takes care of the
following:

- Zephyr keeps ``conf.py`` outside of the source directory (the source
  directory is generated, notably by the ``zephyr.external_content``
  extension), so the sub-build must be given the configuration directory
  explicitly, as well as the tags the main build was run with.

- The sub-build is forced to run *after* the main HTML build (never in
  parallel with it) and shares its doctree directory. Sharing doctrees means
  the sub-build reuses the environment of the main build and therefore does
  not re-run its expensive steps (Doxygen run, hardware features catalog,
  external content sync, etc.) -- and, more importantly, does not run them
  *concurrently* against the same output directories.

- Documents matching one of the ``llms_txt_exclude`` patterns are left out of
  ``llms.txt`` and ``llms-full.txt``. This allows keeping the thousands of
  generated Devicetree bindings pages, which would otherwise dwarf the actual
  documentation content, out of the aggregated outputs. A markdown version of
  excluded pages is still generated alongside their HTML version so that they
  remain individually retrievable.

Configuration options
=====================

This extension registers the upstream ``llms_txt_*`` options (see the
`sphinx-llm documentation <https://github.com/NVIDIA/sphinx-llm>`_) and adds:

- ``llms_txt_exclude``: A list of docname patterns (as understood by
  ``sphinx.util.matching.patmatch``, e.g. ``build/dts/api/bindings/**``) to
  exclude from ``llms.txt`` and ``llms-full.txt``.
"""

import subprocess
import sys
from typing import Any

from docutils import nodes
from sphinx.application import Sphinx
from sphinx.transforms.post_transforms import SphinxPostTransform
from sphinx.util import logging
from sphinx.util.matching import patmatch
from sphinx_llm.txt import MarkdownGenerator

__version__ = "0.1.0"

logger = logging.getLogger(__name__)


class ZephyrMarkdownGenerator(MarkdownGenerator):
    """A Zephyr-build-aware version of ``sphinx_llm.txt.MarkdownGenerator``."""

    def build_markdown_files(self, *args):
        # When invoked from build-finished, args is (app, exception). Don't
        # bother spawning the markdown sub-build if the main build failed.
        if len(args) == 2 and args[1] is not None:
            logger.info("Skipping markdown sub-build due to main build error")
            return

        self.md_build_dir.mkdir(exist_ok=True)

        app = self.app
        sphinx_build_cmd = [
            sys.executable,
            "-m",
            "sphinx",
            "-b",
            "markdown",
            # conf.py does not live in the source directory
            "-c",
            str(app.confdir),
            # Share the main build's doctrees so that its environment (and
            # therefore the result of all the expensive build steps) is reused
            # instead of being recomputed.
            "-d",
            str(app.doctreedir),
            "-j",
            "auto",
        ]

        # Forward the main build's tags. Note that these include the dynamic
        # builder tags (html, format_html, builder_html), which is on purpose:
        # ".. only:: html" content is this way included in the markdown
        # rendition of the pages, keeping it faithful to the HTML output.
        for tag in app.tags:
            sphinx_build_cmd += ["-t", tag]

        if app.config.zephyr_hw_features_vendor_filter:
            sphinx_build_cmd += [
                "-D",
                "zephyr_hw_features_vendor_filter="
                + ",".join(app.config.zephyr_hw_features_vendor_filter),
            ]

        sphinx_build_cmd += [str(app.srcdir), str(self.md_build_dir)]

        logger.info(
            "Spawning additional sphinx subprocess to build markdown files for llms.txt: "
            + " ".join(sphinx_build_cmd)
        )
        logger.info(f"Subprocess output available at: {self.md_build_logfile.name}")

        try:
            with self.md_build_logfile:
                self.md_build_process = subprocess.Popen(
                    sphinx_build_cmd,
                    stdout=self.md_build_logfile,
                    stderr=self.md_build_logfile,
                )
        except Exception as exc:
            logger.error(f"Failed to run sphinx-build subprocess: {exc}")

    def copy_markdown_files(self):
        super().copy_markdown_files()

        patterns = self.app.config.llms_txt_exclude
        if not patterns:
            return

        def is_excluded(md_file):
            docname = self._docname_by_output_file.get(md_file, "")
            return any(patmatch(docname, pattern) for pattern in patterns)

        num_files = len(self.generated_markdown_files)
        self.generated_markdown_files = [
            f for f in self.generated_markdown_files if not is_excluded(f)
        ]
        logger.info(
            f"Excluded {num_files - len(self.generated_markdown_files)} documents "
            "from llms.txt and llms-full.txt"
        )


class MarkdownFriendlyNodesTransform(SphinxPostTransform):
    """Make documents more markdown-friendly before they get written.

    ``sphinx-markdown-builder`` drops the nodes it does not support (they only
    produce an "unknown node type" warning), which would result in content
    missing from the markdown output. Convert the affected node types into
    supported equivalents.
    """

    default_priority = 400
    formats = ("markdown",)

    def run(self, **kwargs: Any) -> None:
        # Generic admonitions and sidebars become a bold title followed by
        # their content.
        for node in [
            *self.document.findall(nodes.admonition),
            *self.document.findall(nodes.sidebar),
        ]:
            children = []
            for child in node.children:
                if isinstance(child, nodes.title):
                    para = nodes.paragraph()
                    para += nodes.strong(text=child.astext())
                    children.append(para)
                elif isinstance(child, nodes.raw):
                    continue
                else:
                    children.append(child)
            node.replace_self(children)

        # Map unsupported admonition flavors to supported ones with a similar
        # meaning.
        for node in list(self.document.findall(nodes.tip)):
            node.replace_self(nodes.hint("", *node.children))

        for node in list(self.document.findall(nodes.caution)):
            node.replace_self(nodes.attention("", *node.children))

        # Figure captions and legends are dropped by the markdown builder;
        # turn them into regular content.
        for node in list(self.document.findall(nodes.caption)):
            para = nodes.paragraph()
            para += nodes.emphasis(text=node.astext())
            node.replace_self(para)

        for node in list(self.document.findall(nodes.legend)):
            node.replace_self(node.children)

        # Abbreviations are inline elements: dropping them would remove words
        # from the middle of sentences. Keep their text.
        for node in list(self.document.findall(nodes.abbreviation)):
            node.replace_self(node.children)


def force_sequential_build(app: Sphinx, config) -> None:
    # Running the markdown sub-build concurrently with the main build is not
    # supported: both builds would race on the shared doctree directory and on
    # the outputs of expensive build steps such as the Doxygen run.
    if config.llms_txt_build_parallel:
        logger.info(
            "llms_txt_build_parallel is not supported by the Zephyr documentation "
            "build, forcing sequential markdown sub-build"
        )
    config.llms_txt_build_parallel = False


def setup(app: Sphinx) -> dict[str, Any]:
    # Register the upstream sphinx_llm.txt options as this extension
    # instantiates its own (specialized) markdown generator instead of setting
    # up the upstream extension (which would spawn a second, misconfigured,
    # sub-build).
    app.add_config_value("llms_txt_enabled", True, "")
    app.add_config_value("llms_txt_description", "", "env")
    app.add_config_value("llms_txt_build_parallel", False, "env")
    app.add_config_value("llms_txt_suffix_mode", "auto", "env")
    app.add_config_value("llms_txt_full_build", True, "env")

    app.add_config_value("llms_txt_exclude", [], "env")

    app.connect("config-inited", force_sequential_build)

    app.add_post_transform(MarkdownFriendlyNodesTransform)

    generator = ZephyrMarkdownGenerator(app)
    generator.setup()

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
