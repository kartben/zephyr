# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Resolve implementation symbols to their function bodies in the tree.

The traceability graph links a requirement to the public API symbols that
implement it (``k_sem_take``, ``k_thread_create``, ...). To model coverage we
need the source range of the code that actually runs, which for a Zephyr syscall
lives in a ``.c`` file, not the header prototype. A syscall-capable API has more
than one body:

* ``z_impl_<sym>`` -- the supervisor-mode implementation, and
* ``z_vrfy_<sym>`` -- the user-mode verifier a ``ZTEST_USER`` test reaches
  instead (it may perform the operation itself without ever calling the z_impl
  body), plus possibly a header ``static inline``.

All bodies are resolved so that a test exercising *any* of them counts as
exercising the implementation.

Coverage line numbers refer to the sources of the build that produced them, so
:class:`Source` reads files at that build's commit (from twister.json's
``environment.zephyr_version``) when it is resolvable, falling back to the
working tree otherwise. This mirrors ``doc/_scripts/traceability_app.py``.
"""

from __future__ import annotations

import fnmatch
import glob
import logging
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

_logger = logging.getLogger(__name__)


@dataclass(frozen=True)
class ImplBody:
    """A resolved function body: tree-relative ``file`` and inclusive line span."""

    file: str
    start: int
    end: int
    variant: str  # impl / vrfy / inline / def


class Source:
    """Reads tree files at the commit a coverage run was built from.

    ``zephyr_version`` is twister.json's ``environment.zephyr_version``
    (``vX.Y.Z-N-g<hash>``); when the hash resolves in this git tree, files are
    read via ``git show <hash>:<path>`` so function ranges line up exactly with
    the coverage data even if the working tree has moved on.
    """

    def __init__(self, root: str, zephyr_version: str = ""):
        self.root = Path(root).resolve()
        self.ref = None
        match = re.search(r"-g([0-9a-f]{7,})$", zephyr_version or "")
        if match:
            ref = match.group(1)
            try:
                subprocess.run(
                    ["git", "rev-parse", "--verify", ref + "^{commit}"],
                    cwd=self.root,
                    check=True,
                    capture_output=True,
                )
                self.ref = ref
            except (subprocess.CalledProcessError, FileNotFoundError, OSError):
                pass
        self._ls = None

    def describe(self) -> str:
        return self.ref or "working tree"

    def list(self, patterns: list[str]) -> list[str]:
        """Tree-relative paths matching any of the glob ``patterns``."""
        if not self.ref:
            out = []
            for pattern in patterns:
                for path in glob.glob(str(self.root / pattern), recursive=True):
                    out.append(str(Path(path).relative_to(self.root)))
            return sorted(set(out))
        if self._ls is None:
            result = subprocess.run(
                ["git", "ls-tree", "-r", "--name-only", self.ref],
                cwd=self.root,
                check=True,
                capture_output=True,
                text=True,
            )
            self._ls = result.stdout.split("\n")
        return sorted(
            {f for f in self._ls for pattern in patterns if fnmatch.fnmatch(f, pattern)}
        )

    def read(self, rel: str) -> str | None:
        """File content at the build ref (or working tree), or ``None``."""
        if not self.ref:
            path = (self.root / rel).resolve()
            if not str(path).startswith(str(self.root)) or not path.is_file():
                return None
            return path.read_text(errors="replace")
        result = subprocess.run(
            ["git", "show", f"{self.ref}:{rel}"],
            cwd=self.root,
            capture_output=True,
            text=True,
            errors="replace",
        )
        return result.stdout if result.returncode == 0 else None


# Files that may carry an implementation body worth resolving.
_IMPL_GLOBS = [
    "kernel/*.c",
    "kernel/**/*.c",
    "include/zephyr/kernel.h",
    "include/zephyr/kernel/**/*.h",
    "include/zephyr/sys/**/*.h",
]

# The stable presentation order of a symbol's bodies.
_VARIANT_ORDER = {"impl": 0, "vrfy": 1, "inline": 2, "def": 3}


def resolve_impl_symbols(source: Source, symbols) -> dict[str, list[ImplBody]]:
    """Map each implementing symbol to its function bodies in the tree.

    A definition starts at column 0 (or, in a header, as a ``static ... inline``)
    and its body ends at the next column-0 closing brace. Returns ``{sym:
    [ImplBody, ...]}`` for symbols that resolve to at least one body (macros and
    prototype-only symbols resolve to none).
    """
    sources: dict[str, list[str]] = {}
    for rel in source.list(_IMPL_GLOBS):
        text = source.read(rel)
        if text is not None:
            sources[rel] = text.split("\n")

    resolved: dict[str, list[ImplBody]] = {}
    for sym in symbols:
        variants = [(f"z_impl_{sym}", "impl"), (f"z_vrfy_{sym}", "vrfy"), (sym, "def")]
        patterns = [
            (
                re.compile(
                    r"^(static +(ALWAYS_INLINE +|__?always_inline +)?inline +)?"
                    r"[A-Za-z_][\w \t\*]*\b" + re.escape(name) + r"\s*\("
                ),
                variant,
            )
            for name, variant in variants
        ]
        bodies: list[ImplBody] = []
        for rel, lines in sources.items():
            in_header = rel.startswith("include/")
            for i, line in enumerate(lines):
                if line.rstrip().endswith(";"):
                    continue
                # In headers only a static-inline body is a definition; anything
                # else is a prototype or macro.
                if in_header and not line.lstrip().startswith("static"):
                    continue
                for pattern, variant in patterns:
                    if pattern.match(line):
                        end = next(
                            (
                                j + 1
                                for j in range(i + 1, min(i + 500, len(lines)))
                                if lines[j] == "}"
                            ),
                            None,
                        )
                        if end:
                            bodies.append(
                                ImplBody(rel, i + 1, end, "inline" if in_header else variant)
                            )
                        break
        if bodies:
            bodies.sort(key=lambda b: (_VARIANT_ORDER[b.variant], b.file, b.start))
            resolved[sym] = bodies

    _logger.info("sources: resolved %d of %d implementation symbol(s) to bodies",
                 len(resolved), len(list(symbols)))
    return resolved


def content_byte_range(text: str, start_line: int, end_line: int) -> tuple[int, int] | None:
    """Inclusive 1-based byte offsets spanned by ``start_line..end_line`` in ``text``."""
    lines = text.splitlines(keepends=True)
    if start_line < 1 or end_line < start_line or start_line > len(lines):
        return None
    end_line = min(end_line, len(lines))
    begin = sum(len(line.encode()) for line in lines[: start_line - 1]) + 1
    end = sum(len(line.encode()) for line in lines[:end_line])
    return begin, end
