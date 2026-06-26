# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Extract used source-line ranges from an ELF file's DWARF debug info.

The public API is :func:`extract_source_ranges`, which returns a mapping from
absolute source-file path to the contiguous line ranges within that file that
actually contributed code to the linked binary.  Callers may restrict results
to a known set of paths via the ``known_paths`` parameter.

Requires ``pyelftools`` (already listed in ``requirements-base.txt``).
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass

_logger = logging.getLogger(__name__)


@dataclass
class SourceRange:
    """A contiguous block of used source lines within a single file.

    ``start_byte`` / ``end_byte`` are byte offsets *within the source file*
    (not within the binary), as required by the SPDX Snippet model.
    """

    path: str
    start_line: int
    end_line: int
    start_byte: int
    end_byte: int


def extract_source_ranges(
    elf_path: str,
    known_paths: set[str] | None = None,
) -> dict[str, list[SourceRange]]:
    """Return contiguous used-line ranges per source file from ELF DWARF debug info.

    Args:
        elf_path: Path to the compiled ELF file (must carry DWARF debug info).
        known_paths: When provided, only source files whose ``os.path.realpath``
            is in this set are included in the result.  Pass ``None`` to collect
            all files referenced by DWARF.

    Returns:
        Dict mapping absolute (realpath) source path to a list of
        :class:`SourceRange` objects, sorted by ``start_line``.  Returns an
        empty dict when the ELF has no DWARF info or cannot be opened.
    """
    try:
        from elftools.elf.elffile import ELFFile
    except ImportError:
        _logger.error(
            "pyelftools is required for snippet extraction; "
            "install it with: pip install pyelftools"
        )
        return {}

    file_lines: dict[str, set[int]] = {}

    try:
        with open(elf_path, "rb") as f:
            elf = ELFFile(f)
            if not elf.has_dwarf_info():
                _logger.warning("ELF file has no DWARF debug info: %s", elf_path)
                return {}
            _collect_lines(elf.get_dwarf_info(), file_lines, known_paths)
    except OSError as exc:
        _logger.error("Cannot open ELF file %s: %s", elf_path, exc)
        return {}

    result: dict[str, list[SourceRange]] = {}
    for path, lines in file_lines.items():
        ranges = _lines_to_ranges(path, sorted(lines))
        if ranges:
            result[path] = ranges

    _logger.debug(
        "Extracted snippet ranges from %s: %d source files, %d total ranges",
        elf_path,
        len(result),
        sum(len(v) for v in result.values()),
    )
    return result


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------


def _collect_lines(
    dwarf,
    file_lines: dict[str, set[int]],
    known_paths: set[str] | None,
) -> None:
    """Walk every DWARF line program and accumulate used line numbers per file."""
    for CU in dwarf.iter_CUs():
        lp = dwarf.line_program_for_CU(CU)
        if lp is None:
            continue

        top_die = CU.get_top_DIE()
        comp_dir_attr = top_die.attributes.get("DW_AT_comp_dir")
        comp_dir = (
            comp_dir_attr.value.decode("utf-8", errors="replace") if comp_dir_attr else ""
        )

        lp_header = lp.header
        file_entries = lp_header.file_entry
        include_dirs = lp_header.include_directory

        for entry in lp.get_entries():
            state = entry.state
            if state is None or state.end_sequence or state.line == 0:
                continue
            try:
                path = _resolve_file(state.file, file_entries, include_dirs, comp_dir)
            except (IndexError, AttributeError):
                continue
            if known_paths is not None and path not in known_paths:
                continue
            file_lines.setdefault(path, set()).add(state.line)


def _resolve_file(
    file_idx: int,
    file_entries,
    include_dirs,
    comp_dir: str,
) -> str:
    """Resolve a 1-based DWARF file-table index to an absolute realpath."""
    fe = file_entries[file_idx - 1]
    name = fe.name.decode("utf-8", errors="replace")
    dir_idx = fe.dir_index
    if dir_idx == 0:
        base = comp_dir
    else:
        raw = include_dirs[dir_idx - 1].decode("utf-8", errors="replace")
        base = raw if os.path.isabs(raw) else os.path.join(comp_dir, raw)
    full = name if os.path.isabs(name) else os.path.join(base, name)
    return os.path.realpath(full)


def _build_line_offsets(path: str) -> dict[int, tuple[int, int]]:
    """Return ``{line_number: (start_byte, end_byte)}`` for every line in *path*.

    Line numbers are 1-based.  ``end_byte`` points to the last byte of the line
    *before* the newline.  Returns an empty dict when the file cannot be read.
    """
    try:
        with open(path, "rb") as f:
            content = f.read()
    except OSError:
        return {}

    offsets: dict[int, tuple[int, int]] = {}
    pos = 0
    for lineno, chunk in enumerate(content.split(b"\n"), start=1):
        offsets[lineno] = (pos, pos + len(chunk))
        pos += len(chunk) + 1  # +1 for the '\n' separator
    return offsets


def _lines_to_ranges(path: str, sorted_lines: list[int]) -> list[SourceRange]:
    """Convert a sorted list of used line numbers to contiguous :class:`SourceRange` objects."""
    if not sorted_lines:
        return []

    offsets = _build_line_offsets(path)
    if not offsets:
        return []

    max_line = max(offsets)
    ranges: list[SourceRange] = []
    start = sorted_lines[0]
    prev = sorted_lines[0]

    for line in sorted_lines[1:]:
        if line > prev + 1:
            r = _make_range(path, start, prev, offsets, max_line)
            if r is not None:
                ranges.append(r)
            start = line
        prev = line

    r = _make_range(path, start, prev, offsets, max_line)
    if r is not None:
        ranges.append(r)

    return ranges


def _make_range(
    path: str,
    start_line: int,
    end_line: int,
    offsets: dict[int, tuple[int, int]],
    max_line: int,
) -> SourceRange | None:
    end_line = min(end_line, max_line)
    if start_line not in offsets or end_line not in offsets:
        return None
    return SourceRange(
        path=path,
        start_line=start_line,
        end_line=end_line,
        start_byte=offsets[start_line][0],
        end_byte=offsets[end_line][1],
    )
