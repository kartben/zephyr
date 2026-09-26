# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Locations shared by the zephyr.* Sphinx extensions.

This is not a Sphinx extension: the leading underscore marks it private to the
package, and it must never appear in conf.py's ``extensions`` list. It imports
nothing from ``zephyr.*`` so that it stays safe to import from any extension,
including from the worker processes zephyr.doxybridge spawns.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import TYPE_CHECKING, Final

if TYPE_CHECKING:
    from sphinx.application import Sphinx

# doc/_extensions/zephyr/_paths.py -> the Zephyr tree
ZEPHYR_BASE: Final[Path] = Path(__file__).resolve().parents[3]
DOC_DIR: Final[Path] = ZEPHYR_BASE / "doc"
SCRIPTS_DIR: Final[Path] = ZEPHYR_BASE / "scripts"

_SCRIPT_PATHS: Final[dict[str, Path]] = {
    "ci": SCRIPTS_DIR / "ci",
    "devicetree": SCRIPTS_DIR / "dts" / "python-devicetree" / "src",
    "doc_scripts": DOC_DIR / "_scripts",
    "kconfig": SCRIPTS_DIR / "kconfig",
    "scripts": SCRIPTS_DIR,
    "twister_harness": SCRIPTS_DIR / "pylib" / "pytest-twister-harness" / "src",
    "west_commands": SCRIPTS_DIR / "west_commands",
}


def add_script_paths(*names: str) -> None:
    """Prepend Zephyr helper script directories to ``sys.path``.

    Idempotent: a directory already on ``sys.path`` is not added again.

    Args:
        names: Keys of :data:`_SCRIPT_PATHS` to make importable.
    """

    for name in names:
        try:
            path = str(_SCRIPT_PATHS[name])
        except KeyError:
            raise ValueError(
                f"Unknown script path '{name}', expected one of {sorted(_SCRIPT_PATHS)}"
            ) from None

        if path not in sys.path:
            sys.path.insert(0, path)


def resources_dir(module_file: str | os.PathLike[str]) -> Path:
    """The ``static`` directory sitting next to *module_file*.

    Args:
        module_file: ``__file__`` of the calling extension.
    """

    return Path(module_file).parent / "static"


def relative_uri(from_dir: Path | str, target: Path | str) -> str:
    """POSIX-style relative URI from *from_dir* to *target*.

    Args:
        from_dir: Directory the URI is resolved against.
        target: Path being linked to.
    """

    return Path(os.path.relpath(target, from_dir)).as_posix()


def outdir_relative_uri(app: Sphinx, target: Path | str, source: Path | str) -> str:
    """URI of *target* as seen from the page built out of *source*.

    Args:
        app: Sphinx application instance.
        target: Path under ``app.outdir`` being linked to.
        source: reStructuredText source file of the linking page.
    """

    page_dir = Path(app.outdir) / os.path.relpath(Path(source).parent, app.srcdir)

    return relative_uri(page_dir, target)
