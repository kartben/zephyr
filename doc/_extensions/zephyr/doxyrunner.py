"""
Doxyrunner Sphinx Plugin
########################

Copyright (c) 2021 Nordic Semiconductor ASA
SPDX-License-Identifier: Apache-2.0

Introduction
============

This Sphinx plugin runs the Doxygen build as part of the Sphinx build process,
so that extensions consuming Doxygen output (``zephyr.doxybridge``,
``zephyr.doxyxref``, ``zephyr.api_overview``) find it in place. The principal
features offered by this plugin are:

- Doxygen build is run before Sphinx reads input files
- Doxyfile can be optionally pre-processed so that variables can be inserted
- Changes in the Doxygen input files are tracked so that Doxygen build is only
  run if necessary.

Configuration options
=====================

- ``doxyrunner_doxygen``: Path to the Doxygen binary.
- ``doxyrunner_silent``: If Doxygen output should be logged or not. Note that
  this option may not have any effect if ``QUIET`` is set to ``YES``.
- ``doxyrunner_projects``: Dictionary specifying projects, keys being project
  name and values a dictionary with the following keys:

  - ``doxyfile``: Path to Doxyfile.
  - ``outdir``: Doxygen build output directory (inserted to ``OUTPUT_DIRECTORY``),
    required.
  - ``outdir_var``: Variable representing the Doxygen build output directory,
    as used by ``OUTPUT_DIRECTORY``. This can be useful if other Doxygen
    variables reference to the output directory.
  - ``fmt``: Flag to indicate if Doxyfile should be formatted.
  - ``fmt_vars``: Format variables dictionary (name: value).
  - ``fmt_pattern``: Format pattern.
"""

import hashlib
import re
import shlex
import shutil
import tempfile
from dataclasses import dataclass
from functools import cache
from pathlib import Path
from subprocess import PIPE, STDOUT, Popen
from typing import Any

from sphinx.application import Sphinx
from sphinx.config import Config
from sphinx.environment import BuildEnvironment
from sphinx.errors import ExtensionError
from sphinx.util import logging

__version__ = "0.1.0"


logger = logging.getLogger(__name__)


def hash_file(file: Path) -> str:
    """Compute the hash (SHA256) of a file in text mode.

    Args:
        file: File to be hashed.

    Returns:
        Hash.
    """

    with open(file, encoding="utf-8") as f:
        sha256 = hashlib.sha256(f.read().encode("utf-8"))

    return sha256.hexdigest()


def get_doxygen_option(doxyfile: str, option: str) -> list[str]:
    """Obtain the value of a Doxygen option.

    Args:
        doxyfile: Content of the Doxyfile.
        option: Option to be retrieved.

    Notes:
        Does not support appended values.

    Returns:
        Option values.
    """

    option_re = re.compile(r"^\s*([A-Z0-9_]+)\s*=\s*(.*)$")
    multiline_re = re.compile(r"^\s*(.*)$")

    values = []
    found = False
    finished = False
    for line in doxyfile.splitlines():
        if not found:
            m = option_re.match(line)
            if not m or m.group(1) != option:
                continue

            found = True
            value = m.group(2)
        else:
            m = multiline_re.match(line)
            if not m:
                raise ValueError(f"Unexpected line content: {line}")

            value = m.group(1)

        # check if it is a multiline value
        finished = not value.endswith("\\")

        # strip backslash
        if not finished:
            value = value[:-1]

        # split values
        values += shlex.split(value.replace("\\", "\\\\"))

        if finished:
            break

    return values


@dataclass(frozen=True)
class DoxygenOutput:
    """Resolved output directories of a single Doxygen project."""

    root: Path
    html: Path
    xml: Path


@cache
def _output_subdirs(doxyfile: Path) -> tuple[str, str]:
    """HTML_OUTPUT and XML_OUTPUT of a Doxyfile, falling back to Doxygen's defaults."""

    content = doxyfile.read_text()
    html = get_doxygen_option(content, "HTML_OUTPUT")
    xml = get_doxygen_option(content, "XML_OUTPUT")

    return html[0] if html else "html", xml[0] if xml else "xml"


def doxygen_outputs(config: Config) -> dict[str, DoxygenOutput]:
    """Resolved Doxygen output directories, keyed by project name.

    Derived from ``doxyrunner_projects`` alone, with no dependency on build
    state, so any extension may call this in any build phase. Empty when
    ``doxyrunner_skip`` is set: nothing is generated, so there is nothing to
    consume.

    Args:
        config: Sphinx configuration.
    """

    if config.doxyrunner_skip:
        return {}

    outputs = {}
    for name, project in config.doxyrunner_projects.items():
        if not project.get("outdir"):
            raise ExtensionError(f"doxyrunner_projects['{name}'] has no 'outdir'")

        root = Path(project["outdir"])
        html_subdir, xml_subdir = _output_subdirs(Path(project["doxyfile"]))
        outputs[name] = DoxygenOutput(root=root, html=root / html_subdir, xml=root / xml_subdir)

    return outputs


def doxygen_input_changed(env: BuildEnvironment, project: str) -> bool:
    """Whether Doxygen input changed for *project* during this build.

    Unknown projects report ``True``, so a consumer that runs without
    :func:`doxygen_build` having populated the environment re-reads rather than
    trusting a cache that was never filled.

    Args:
        env: Sphinx build environment.
        project: Doxygen project name.
    """

    return getattr(env, "doxygen_input_changed", {}).get(project, True)


def process_doxyfile(
    doxyfile: str,
    outdir: Path,
    silent: bool,
    fmt: bool = False,
    fmt_pattern: str | None = None,
    fmt_vars: dict[str, str] | None = None,
    outdir_var: str | None = None,
) -> str:
    """Process Doxyfile.

    Notes:
        OUTPUT_DIRECTORY, WARN_FORMAT and QUIET are overridden to satisfy
        extension operation needs.

    Args:
        doxyfile: Path to the Doxyfile.
        outdir: Output directory of the Doxygen build.
        silent: If Doxygen should be run in quiet mode or not.
        fmt: If Doxyfile should be formatted.
        fmt_pattern: Format pattern.
        fmt_vars: Format variables.
        outdir_var: Variable representing output directory.

     Returns:
        Processed Doxyfile content.
    """

    with open(doxyfile) as f:
        content = f.read()

    content = re.sub(
        r"^\s*OUTPUT_DIRECTORY\s*=.*$",
        f"OUTPUT_DIRECTORY={outdir.as_posix()}",
        content,
        flags=re.MULTILINE,
    )

    content = re.sub(
        r"^\s*WARN_FORMAT\s*=.*$",
        'WARN_FORMAT="$file:$line: $text"',
        content,
        flags=re.MULTILINE,
    )

    content = re.sub(
        r"^\s*QUIET\s*=.*$",
        "QUIET=" + ("YES" if silent else "NO"),
        content,
        flags=re.MULTILINE,
    )

    if fmt:
        if not fmt_pattern or not fmt_vars:
            raise ValueError("Invalid formatting pattern or variables")

        if outdir_var:
            fmt_vars = fmt_vars.copy()
            fmt_vars[outdir_var] = outdir.as_posix()

        for var, value in fmt_vars.items():
            content = content.replace(fmt_pattern.format(var), value)

    return content


def doxygen_input_has_changed(env: BuildEnvironment, name, doxyfile: str) -> bool:
    """Check if Doxygen input files have changed.

    Args:
        env: Sphinx build environment instance.
        doxyfile: Doxyfile content.

    Returns:
        True if changed, False otherwise.
    """

    # obtain Doxygen input files and patterns
    input_files = get_doxygen_option(doxyfile, "INPUT")
    if not input:
        raise ValueError("No INPUT set in Doxyfile")

    file_patterns = get_doxygen_option(doxyfile, "FILE_PATTERNS")
    if not file_patterns:
        raise ValueError("No FILE_PATTERNS set in Doxyfile")

    # build a set with input files hash
    cache = set()
    for file in input_files:
        path = Path(file)
        if path.is_file():
            cache.add(hash_file(path))
        else:
            for pattern in file_patterns:
                for p_file in path.glob("**/" + pattern):
                    cache.add(hash_file(p_file))

    if not hasattr(env, "doxyrunner_cache"):
        env.doxyrunner_cache = dict()

    # check if any file has changed
    if env.doxyrunner_cache.get(name) == cache:
        return False

    # store current state
    env.doxyrunner_cache[name] = cache

    return True


def process_doxygen_output(line: str, silent: bool) -> None:
    """Process a line of Doxygen program output.

    This function will map Doxygen output to the Sphinx logger output. Errors
    and warnings will be converted to Sphinx errors and warnings. Other
    messages, if not silent, will be mapped to the info logger channel.

    Args:
        line: Doxygen program line.
        silent: True if regular messages should be logged, False otherwise.
    """

    m = re.match(r"(.*):(\d+): ([a-z]+): (.*)", line)
    if m:
        type = m.group(3)
        message = f"{m.group(1)}:{m.group(2)}: {m.group(4)}"
        if type == "error":
            logger.error(message)
        elif type == "warning":
            logger.warning(message)
        else:
            logger.info(message)
    elif not silent:
        logger.info(line)


def run_doxygen(doxygen: str, doxyfile: str, silent: bool = False) -> None:
    """Run Doxygen build.

    Args:
        doxygen: Path to Doxygen binary.
        doxyfile: Doxyfile content.
        silent: If Doxygen output should be logged or not.
    """

    with tempfile.NamedTemporaryFile("w", delete=False) as f_doxyfile:
        f_doxyfile.write(doxyfile)
        f_doxyfile_name = f_doxyfile.name

    p = Popen([doxygen, f_doxyfile_name], stdout=PIPE, stderr=STDOUT, encoding="utf-8")
    while True:
        line = p.stdout.readline()  # type: ignore
        if line:
            process_doxygen_output(line.rstrip(), silent)
        if p.poll() is not None:
            break

    Path(f_doxyfile_name).unlink()

    if p.returncode:
        raise OSError(f"Doxygen process returned non-zero ({p.returncode})")


def clean_outdir(app: Sphinx, outdir: Path) -> None:
    """Remove a Doxygen output directory before a fresh run.

    Doxygen never prunes output for inputs that have gone away, so the previous
    run's tree is discarded wholesale.

    Args:
        app: Sphinx application instance.
        outdir: Doxygen build output directory.
    """

    outdir = outdir.resolve()
    for reserved in (Path(app.outdir).resolve(), Path(app.srcdir).resolve()):
        if outdir == reserved or outdir in reserved.parents:
            raise ExtensionError(f"Refusing to remove Doxygen outdir {outdir}: it holds {reserved}")

    shutil.rmtree(outdir, ignore_errors=True)


def doxygen_build(app: Sphinx) -> None:
    """Doxyrunner entry point.

    Args:
        app: Sphinx application instance.
    """

    if app.config.doxyrunner_skip:
        logger.info("Doxygen build skipped (doxyrunner_skip is set).")
        return

    outputs = doxygen_outputs(app.config)

    for name, config in app.config.doxyrunner_projects.items():
        outdir = outputs[name].root

        logger.info("Preparing Doxyfile...")
        doxyfile = process_doxyfile(
            config["doxyfile"],
            outdir,
            app.config.doxyrunner_silent,
            config.get("fmt", False),
            config.get("fmt_pattern", "@{}@"),
            config.get("fmt_vars", {}),
            config.get("outdir_var"),
        )

        logger.info(f"Checking if Doxygen needs to be run for {name}...")
        if not hasattr(app.env, "doxygen_input_changed"):
            app.env.doxygen_input_changed = dict()

        app.env.doxygen_input_changed[name] = doxygen_input_has_changed(app.env, name, doxyfile)
        if not app.env.doxygen_input_changed[name]:
            logger.info(f"Doxygen build for {name} will be skipped (no changes)!")
            continue

        clean_outdir(app, outdir)
        outdir.mkdir(parents=True, exist_ok=True)

        logger.info(f"Running Doxygen for {name}...")
        run_doxygen(
            app.config.doxyrunner_doxygen,
            doxyfile,
            app.config.doxyrunner_silent,
        )


def setup(app: Sphinx) -> dict[str, Any]:
    app.add_config_value("doxyrunner_doxygen", "doxygen", "env")
    app.add_config_value("doxyrunner_silent", True, "")
    app.add_config_value("doxyrunner_projects", {}, "")
    app.add_config_value("doxyrunner_skip", False, "env")

    app.connect("builder-inited", doxygen_build)

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
