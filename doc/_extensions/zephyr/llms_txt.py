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

``sphinx_llm.txt`` works by spawning a second Sphinx build using a markdown
builder and merging its output into the HTML output directory. When run
sequentially (``llms_txt_build_parallel = False``, enforced by ``conf.py``),
this sub-build shares the main build's doctree directory so that its
environment -- and therefore the result of all the expensive build steps
(Doxygen run, Kconfig database, hardware features catalog, external content
sync) -- is reused instead of being recomputed.

This wrapper specializes the sub-build for Zephyr's documentation build:

- The tags of the main build are forwarded to the sub-build. ``conf.py``
  derives parts of the configuration (extensions, generated content) from
  them, and a configuration mismatch would invalidate the shared build
  environment, forcing the sub-build to redo the work the doctree sharing is
  precisely meant to reuse. The dynamic builder tags (``html``, ...) are
  forwarded too, so that ``.. only:: html`` content appears in the markdown
  rendition of the pages, keeping it faithful to the HTML output.

- The ``zephyr_hw_features_vendor_filter`` command-line override (``-D``),
  which would otherwise not reach the sub-build, is forwarded for the same
  reason.

- The sub-build runs with parallel writing enabled (``-j auto``), like the
  main build.

- Documents are made more markdown-friendly before they get written:
  ``sphinx-markdown-builder`` drops the node types it does not support, so
  they are converted into supported equivalents beforehand.
"""

import subprocess
import sys
import tempfile
from typing import Any

import sphinx_llm.txt
from docutils import nodes
from sphinx.application import Sphinx
from sphinx.transforms.post_transforms import SphinxPostTransform
from sphinx.util import logging
from sphinx_llm.markdown_builder import SphinxLlmMarkdownBuilder
from sphinx_llm.txt import MarkdownGenerator

__version__ = "0.2.0"

logger = logging.getLogger(__name__)


class ZephyrMarkdownGenerator(MarkdownGenerator):
    """A Zephyr-build-aware version of ``sphinx_llm.txt.MarkdownGenerator``."""

    def build_markdown_files(self, app=None, exception=None):
        if exception is not None:
            logger.info("Skipping markdown build because the primary build failed")
            return

        self.md_build_dir.mkdir(exist_ok=True)
        # The log file is closed when the subprocess is spawned below; it is
        # kept on disk so that it can be referenced after a failed build.
        self.md_build_logfile = tempfile.NamedTemporaryFile(  # noqa: SIM115
            mode="w", delete=False, prefix="sphinx_llm_output_", suffix=".log"
        )

        app = self.app
        sphinx_build_cmd = [
            sys.executable,
            "-m",
            "sphinx",
            "-b",
            SphinxLlmMarkdownBuilder.name,
            "-t",
            "sphinx_llm_markdown",
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

        # Forward the main build's tags so that conf.py evaluates to the same
        # configuration in the sub-build (a mismatch would invalidate the
        # shared build environment). These include the dynamic builder tags
        # (html, format_html, builder_html), which is on purpose: this way
        # ".. only:: html" content is included in the markdown rendition of
        # the pages, keeping it faithful to the HTML output.
        for tag in app.tags:
            sphinx_build_cmd += ["-t", tag]

        # Command-line configuration overrides (-D) do not reach the
        # sub-build; forward the one that affects generated content.
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
    # Substitute the Zephyr-build-aware markdown generator for the upstream
    # one before setting up the sphinx_llm.txt extension, which instantiates
    # it.
    sphinx_llm.txt.MarkdownGenerator = ZephyrMarkdownGenerator
    app.setup_extension("sphinx_llm.txt")

    # sphinx_llm.txt only loads sphinx_markdown_builder in the markdown
    # sub-build. Load it in the main build as well so that both builds run
    # with the same configuration values: the markdown_* options it registers
    # would otherwise be seen as a configuration change by the sub-build,
    # needlessly invalidating the shared build environment.
    app.setup_extension("sphinx_markdown_builder")

    app.connect("config-inited", force_sequential_build)

    app.add_post_transform(MarkdownFriendlyNodesTransform)

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
