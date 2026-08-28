# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Extract used source information from an ELF file's DWARF debug info.

The DWARF line programs of a linked image tell us which source lines actually
contributed code to it, and the debug information entries tell us which routine
each of those lines became.  A single pass over the image yields both:

* ``file_lines`` is the raw ``{source path: {used line}}`` mapping.  Its key set
  is the set of source files present in the final image, which the
  ``prune-sources`` analysis uses to drop sources that contributed nothing.
* ``routines`` folds the same lines into named :class:`Routine` objects holding
  contiguous :class:`SourceRange` blocks, which the ``snippets`` analysis emits
  as a source file → routine → line range hierarchy of SPDX Snippets.

Requires ``pyelftools`` (already listed in ``requirements-base.txt``).
"""

from __future__ import annotations

import bisect
import logging
import os
from contextlib import contextmanager
from dataclasses import dataclass, field
from functools import lru_cache

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


@dataclass
class Routine:
    """A named routine that contributed code to the image.

    ``ranges`` holds the used source ranges attributed to this routine.  They
    usually live in ``decl_file``, but not always: code a routine picks up from
    a macro carries the line numbers of the header the macro is written in, so
    a range's own ``path`` is authoritative.

    Attributes:
        name: Routine name as DWARF records it.
        decl_file: Realpath of the file that *defines* the routine.
        decl_line: 1-based line of the definition, when DWARF records one.
        ranges: Used source ranges attributed to this routine.
        inlined_only: True when the image holds no out-of-line copy, i.e. every
            use was inlined.  Such routines are invisible to a map file.
        declared_in: Realpath of the header that *declares* the routine, when
            DWARF carries a separate declaration for it elsewhere.
        declared_line: 1-based line of that declaration.
    """

    name: str
    decl_file: str
    decl_line: int | None = None
    ranges: list[SourceRange] = field(default_factory=list)
    inlined_only: bool = False
    declared_in: str | None = None
    declared_line: int | None = None

    @property
    def line_extent(self) -> tuple[int, int] | None:
        """``(first, last)`` used line within ``decl_file``, or None."""
        own = [r for r in self.ranges if r.path == self.decl_file]
        if not own:
            return None
        return (min(r.start_line for r in own), max(r.end_line for r in own))


@dataclass
class ImageAnalysis:
    """What one DWARF pass over a linked image yields.

    Attributes:
        file_lines: Used 1-based line numbers per source file realpath.
        routines: Routines that contributed code, sorted by definition site.
            Empty unless routine extraction was requested.
    """

    file_lines: dict[str, set[int]] = field(default_factory=dict)
    routines: list[Routine] = field(default_factory=list)


def analyze_image(
    elf_path: str,
    known_paths: set[str] | None = None,
    with_routines: bool = False,
) -> ImageAnalysis:
    """Read a linked image's DWARF debug info in a single pass.

    Args:
        elf_path: Path to the linked image (must carry DWARF debug info).
        known_paths: When provided, only source files whose ``os.path.realpath``
            is in this set are included.  Pass ``None`` to collect everything
            DWARF references.
        with_routines: Also walk the debug information entries and attribute
            each used line to the routine it became.  This is the expensive
            part of the pass, so it is opt-in.

    Returns:
        An :class:`ImageAnalysis`.  Its fields are empty when the image has no
        DWARF info or cannot be read.
    """
    result = ImageAnalysis()

    with _dwarf_info(elf_path) as dwarf:
        if dwarf is None:
            return result

        # (name, decl_file, decl_line) -> {source path: {used line}}
        routine_lines: dict[_Identity, dict[str, set[int]]] = {}
        declarations: dict[str, set[tuple[str, int | None]]] = {}
        out_of_line: set[_Identity] = set()
        scopes_by_cu: dict[int, list] = {}
        sites: dict[str, dict[int, _Identity]] = {}

        # Where the routines are must be known before any line can be placed:
        # which routine a line belongs to is settled by the source structure,
        # and the PC ranges only arbitrate the lines that sit outside every
        # routine body.
        if with_routines:
            for cu in dwarf.iter_CUs():
                line_program = dwarf.line_program_for_CU(cu)
                if line_program is None:
                    continue
                resolve = _file_resolver(cu, line_program)
                scopes_by_cu[cu.cu_offset] = _cu_scopes(
                    cu, dwarf, resolve, declarations, out_of_line, sites
                )
        ladder = {path: sorted(by_line) for path, by_line in sites.items()}

        for cu in dwarf.iter_CUs():
            line_program = dwarf.line_program_for_CU(cu)
            if line_program is None:
                continue
            _walk_line_program(
                line_program,
                _file_resolver(cu, line_program),
                known_paths,
                result.file_lines,
                scopes_by_cu.get(cu.cu_offset, ()),
                (sites, ladder),
                routine_lines,
            )

        if with_routines:
            result.routines = _build_routines(routine_lines, declarations, out_of_line)

    _logger.debug(
        "Analyzed %s: %d source file(s), %d routine(s)",
        elf_path,
        len(result.file_lines),
        len(result.routines),
    )
    return result


def collect_used_lines(
    elf_path: str,
    known_paths: set[str] | None = None,
) -> dict[str, set[int]]:
    """Return the used source lines per file from an ELF's DWARF debug info.

    Args:
        elf_path: Path to the compiled ELF file (must carry DWARF debug info).
        known_paths: When provided, only source files whose ``os.path.realpath``
            is in this set are included in the result.  Pass ``None`` to collect
            all files referenced by DWARF.

    Returns:
        Dict mapping absolute (realpath) source path to the set of 1-based line
        numbers used from that file.  Returns an empty dict when the ELF has no
        DWARF info or cannot be opened.
    """
    return analyze_image(elf_path, known_paths).file_lines


def extract_routines(
    elf_path: str,
    known_paths: set[str] | None = None,
) -> list[Routine]:
    """Return the routines that contributed code to a linked image.

    Args:
        elf_path: Path to the linked image (must carry DWARF debug info).
        known_paths: When provided, only ranges in source files whose
            ``os.path.realpath`` is in this set are kept.

    Returns:
        Routines sorted by definition site.  Empty when the image has no DWARF
        info or cannot be read.
    """
    return analyze_image(elf_path, known_paths, with_routines=True).routines


def line_byte_range(path: str, start_line: int, end_line: int) -> tuple[int, int] | None:
    """Return the ``(start_byte, end_byte)`` span of a line range within a file.

    Args:
        path: Source file to measure.
        start_line: First 1-based line of the span.
        end_line: Last 1-based line of the span, clamped to the file's length.

    Returns:
        Byte offsets within the file, or ``None`` when it cannot be read or the
        span falls outside it.
    """
    offsets = _build_line_offsets(path)
    if not offsets:
        return None
    end_line = min(end_line, max(offsets))
    if start_line not in offsets or end_line not in offsets:
        return None
    return (offsets[start_line][0], offsets[end_line][1])


def ranges_from_line_map(file_lines: dict[str, set[int]]) -> dict[str, list[SourceRange]]:
    """Fold a ``{path: {used lines}}`` mapping into contiguous source ranges.

    Args:
        file_lines: Used lines per source file, as returned by
            :func:`collect_used_lines`.

    Returns:
        Dict mapping absolute (realpath) source path to a list of
        :class:`SourceRange` objects, sorted by ``start_line``.  Files whose
        ranges cannot be resolved (e.g. the source is unreadable) are omitted.
    """
    result: dict[str, list[SourceRange]] = {}
    for path, lines in file_lines.items():
        ranges = _lines_to_ranges(path, sorted(lines))
        if ranges:
            result[path] = ranges
    return result


def extract_source_ranges(
    elf_path: str,
    known_paths: set[str] | None = None,
) -> dict[str, list[SourceRange]]:
    """Return contiguous used-line ranges per source file from ELF DWARF debug info.

    Convenience wrapper over :func:`collect_used_lines` followed by
    :func:`ranges_from_line_map`.

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
    return ranges_from_line_map(collect_used_lines(elf_path, known_paths))


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

# (name, decl_file realpath, decl_line) identifies a routine across CUs.
type _Identity = tuple[str, str, int | None]


@contextmanager
def _dwarf_info(elf_path: str):
    """Yield an ELF's DWARFInfo, or None when it cannot be read."""
    try:
        from elftools.common.exceptions import ELFError
        from elftools.elf.elffile import ELFFile
    except ImportError:
        _logger.error(
            "pyelftools is required for --analyze-elf; install it with: pip install pyelftools"
        )
        yield None
        return

    try:
        with open(elf_path, "rb") as f:
            elf = ELFFile(f)
            if not elf.has_dwarf_info():
                _logger.warning("ELF file has no DWARF debug info: %s", elf_path)
                yield None
                return
            if elf.header.e_type == "ET_REL":
                _logger.warning(
                    "%s is a relocatable object, not a linked image; its debug info "
                    "describes the inputs to the final link rather than what shipped. "
                    "Pass --elf-file with the linked image (zephyr.exe on native_sim).",
                    elf_path,
                )
            # Debug sections are read as-is: only line programs and DIE
            # attributes are needed, and applying relocations fails outright on
            # the relocation types some Zephyr targets emit.
            yield elf.get_dwarf_info(relocate_dwarf_sections=False)
    except OSError as exc:
        _logger.error("Cannot open ELF file %s: %s", elf_path, exc)
        yield None
    except ELFError as exc:
        _logger.error("Cannot read debug info from %s: %s", elf_path, exc)
        yield None


def _file_resolver(cu, line_program):
    """Return a memoised ``file index -> realpath`` resolver for one CU."""
    comp_dir_attr = cu.get_top_DIE().attributes.get("DW_AT_comp_dir")
    comp_dir = comp_dir_attr.value.decode("utf-8", errors="replace") if comp_dir_attr else ""
    header = line_program.header
    file_entries = header.file_entry
    include_dirs = header.include_directory
    version = header.version
    cache: dict[int, str | None] = {}

    def resolve(index: int) -> str | None:
        if index not in cache:
            try:
                cache[index] = _resolve_file(index, file_entries, include_dirs, comp_dir, version)
            except (IndexError, AttributeError):
                cache[index] = None
        return cache[index]

    return resolve


def _walk_line_program(
    line_program,
    resolve,
    known_paths: set[str] | None,
    file_lines: dict[str, set[int]],
    scopes,
    definitions,
    routine_lines,
) -> None:
    """Record every used line, and attribute it to a routine when scopes are known."""
    lows, reach = _scope_index(scopes)

    for entry in line_program.get_entries():
        state = entry.state
        if state is None or state.end_sequence or state.line == 0:
            continue
        path = resolve(state.file)
        if path is None:
            continue
        if known_paths is not None and path not in known_paths:
            continue
        file_lines.setdefault(path, set()).add(state.line)
        if not scopes:
            continue
        identity = _owning_routine(
            state.address, state.line, path, scopes, lows, reach, definitions
        )
        if identity is not None:
            routine_lines.setdefault(identity, {}).setdefault(path, set()).add(state.line)


def _scope_index(scopes):
    """Return ``(lows, reach)`` lookup arrays for a CU's sorted scope list.

    ``reach[i]`` is the highest end address among scopes ``0..i``, which lets a
    backwards scan stop as soon as no earlier scope can still cover an address.
    """
    lows: list[int] = []
    reach: list[int] = []
    highest = 0
    for low, high, _, _ in scopes:
        lows.append(low)
        highest = max(highest, high)
        reach.append(highest)
    return lows, reach


def _textual_owner(path, line, definitions) -> _Identity | None:
    """Return the routine whose source body textually contains ``path:line``.

    Which routine a line of code sits in is a property of the source, not of
    the generated instructions: it is the last routine defined at or above that
    line in that file.  Reading it off the definition sites keeps attribution
    stable no matter how the optimiser later moved, split or duplicated the
    code.
    """
    sites, ladder = definitions
    decl_lines = ladder.get(path)
    if not decl_lines:
        return None
    index = bisect.bisect_right(decl_lines, line) - 1
    if index < 0:
        return None
    return sites[path][decl_lines[index]]


def _owning_routine(address, line, path, scopes, lows, reach, definitions) -> _Identity | None:
    """Return the identity of the routine whose code lives at *address*.

    A line inside some routine's body belongs to that routine, and the textual
    owner says which one.  The PC ranges are what confirm the routine is really
    live at this address, and what resolve the lines that belong to no routine
    at all -- the body of a macro, which the enclosing routine expanded.
    """
    owner = _textual_owner(path, line, definitions)
    outermost = None
    for i in range(bisect.bisect_right(lows, address) - 1, -1, -1):
        if reach[i] <= address:
            break
        low, high, depth, identity = scopes[i]
        if not low <= address < high:
            continue
        if identity == owner:
            return owner
        if outermost is None or depth < outermost[0]:
            outermost = (depth, identity)
    if outermost is None:
        return owner
    # The optimiser can leave a routine's own lines outside the PC range DWARF
    # gives it; trust the source structure over the addresses, but only within
    # the file that the enclosing routine was compiled from -- a line from
    # anywhere else reached here through a macro, and belongs to its expander.
    if owner is not None and path == outermost[1][1]:
        return owner
    return outermost[1]


def _cu_scopes(cu, dwarf, resolve, declarations, out_of_line, sites):
    """Return one CU's routine PC ranges, sorted by start address.

    Each scope is ``(low, high, depth, identity)``.  The caller's collections
    accumulate what the whole image contributes: ``declarations`` gathers every
    site a name is declared at, ``out_of_line`` holds the identities the image
    keeps a standalone copy of, and ``sites`` indexes every definition by the
    file and line it was written at.
    """
    cu_dies = {die.offset: die for die in cu.iter_DIEs()}
    scopes: list[tuple[int, int, int, _Identity]] = []

    for die in cu_dies.values():
        if die.tag == "DW_TAG_subprogram":
            if "DW_AT_declaration" in die.attributes:
                identity = _routine_identity(die, cu_dies, resolve)
                if identity is not None:
                    declarations.setdefault(identity[0], set()).add((identity[1], identity[2]))
                continue
            depth = 0
        elif die.tag == "DW_TAG_inlined_subroutine":
            # Nesting depth decides which routine owns an address when an
            # inlined body sits inside another one.
            depth = _inline_depth(die)
        else:
            continue

        identity = _routine_identity(die, cu_dies, resolve)
        if identity is None:
            continue
        if identity[2] is not None:
            sites.setdefault(identity[1], {}).setdefault(identity[2], identity)

        ranges = _pc_ranges(die, dwarf)
        if not ranges:
            continue
        if depth == 0:
            out_of_line.add(identity)
        for low, high in ranges:
            scopes.append((low, high, depth, identity))

    scopes.sort(key=lambda scope: scope[0])
    return scopes


def _inline_depth(die) -> int:
    """Return how many inlined frames enclose *die*, itself included."""
    depth = 0
    node = die
    while node is not None and node.tag != "DW_TAG_compile_unit":
        if node.tag == "DW_TAG_inlined_subroutine":
            depth += 1
        node = node.get_parent()
    return depth


def _die_attr_chain(die, cu_dies, attr):
    """Read *attr* from a DIE, following abstract_origin/specification links.

    An inlined instance carries no name of its own; it points at the abstract
    instance that does.  Concrete out-of-line copies of inlinable functions do
    the same.
    """
    seen: set[int] = set()
    while die is not None and die.offset not in seen:
        seen.add(die.offset)
        attributes = die.attributes
        if attr in attributes:
            return attributes[attr]
        die = next(
            (
                cu_dies.get(attributes[link].value + die.cu.cu_offset)
                for link in ("DW_AT_abstract_origin", "DW_AT_specification")
                if link in attributes
            ),
            None,
        )
    return None


def _routine_identity(die, cu_dies, resolve) -> _Identity | None:
    """Return ``(name, decl_file, decl_line)`` for a subprogram-like DIE."""
    name_attr = _die_attr_chain(die, cu_dies, "DW_AT_name")
    file_attr = _die_attr_chain(die, cu_dies, "DW_AT_decl_file")
    if name_attr is None or file_attr is None:
        return None
    decl_file = resolve(file_attr.value)
    if decl_file is None:
        return None
    line_attr = _die_attr_chain(die, cu_dies, "DW_AT_decl_line")
    return (
        name_attr.value.decode("utf-8", errors="replace"),
        decl_file,
        line_attr.value if line_attr is not None else None,
    )


def _pc_ranges(die, dwarf) -> list[tuple[int, int]]:
    """Return the ``[low, high)`` PC ranges a DIE covers."""
    attributes = die.attributes

    if "DW_AT_ranges" in attributes:
        return _pc_ranges_from_list(die, dwarf, attributes["DW_AT_ranges"].value)

    if "DW_AT_low_pc" not in attributes:
        return []
    low = attributes["DW_AT_low_pc"].value
    high_attr = attributes.get("DW_AT_high_pc")
    if high_attr is None:
        return []
    # From DWARF 4 on, a non-address form means an offset from low_pc.
    high = (
        high_attr.value
        if high_attr.form in ("DW_FORM_addr", "DW_FORM_addrx")
        else low + high_attr.value
    )
    return [(low, high)] if high > low else []


def _pc_ranges_from_list(die, dwarf, offset) -> list[tuple[int, int]]:
    """Resolve a DW_AT_ranges offset into absolute ``[low, high)`` pairs."""
    try:
        range_lists = dwarf.range_lists()
        if range_lists is None:
            return []
        entries = range_lists.get_range_list_at_offset(offset, cu=die.cu)
    except Exception:  # noqa: BLE001 - a malformed range list must not abort the scan
        _logger.debug("Unreadable DW_AT_ranges at offset %s; skipping", offset)
        return []

    cu_low = die.cu.get_top_DIE().attributes.get("DW_AT_low_pc")
    base = cu_low.value if cu_low is not None else 0
    out = []
    for entry in entries:
        kind = type(entry).__name__
        if kind == "BaseAddressEntry":
            base = entry.base_address
        elif kind == "RangeEntry":
            if getattr(entry, "is_absolute", False):
                out.append((entry.begin_offset, entry.end_offset))
            else:
                out.append((base + entry.begin_offset, base + entry.end_offset))
    return [(low, high) for low, high in out if high > low]


def _build_routines(routine_lines, declarations, out_of_line) -> list[Routine]:
    """Turn attributed line sets into :class:`Routine` objects."""
    routines: list[Routine] = []
    for identity, per_file in routine_lines.items():
        name, decl_file, decl_line = identity
        ranges: list[SourceRange] = []
        for path, used in per_file.items():
            ranges.extend(_lines_to_ranges(path, sorted(used)))
        if not ranges:
            continue
        routine = Routine(
            name=name,
            decl_file=decl_file,
            decl_line=decl_line,
            ranges=sorted(ranges, key=lambda r: (r.path, r.start_line)),
            inlined_only=identity not in out_of_line,
        )
        declared = _pick_declaration(declarations.get(name), decl_file)
        if declared is not None:
            routine.declared_in, routine.declared_line = declared
        routines.append(routine)

    return sorted(routines, key=lambda r: (r.decl_file, r.decl_line or 0, r.name))


_HEADER_EXTS = frozenset({".h", ".hh", ".hpp", ".hxx", ".h++", ".inc"})


def _pick_declaration(sites, decl_file):
    """Choose the declaration site that best explains where a routine is announced.

    A name can be declared in several places -- the header that publishes it,
    but also a forward declaration inside some ``.c``.  Only a header says
    something the definition does not, so that is the only kind kept; a static
    inline declares itself in the file it is defined in, and is filtered out by
    the same rule.
    """
    headers = [
        site
        for site in sites or ()
        if site[0] != decl_file
        and site[1] is not None
        and os.path.splitext(site[0])[1].lower() in _HEADER_EXTS
    ]
    return min(headers) if headers else None


def _resolve_file(
    file_idx: int,
    file_entries,
    include_dirs,
    comp_dir: str,
    version: int,
) -> str:
    """Resolve a DWARF file-table index to an absolute realpath.

    Up to v4 the file table is 1-based and directory 0 is implicitly the
    compilation directory; from v5 both tables are 0-based and directory 0 is
    an explicit entry holding it.
    """
    if version >= 5:
        fe = file_entries[file_idx]
    else:
        fe = file_entries[file_idx - 1]

    name = fe.name.decode("utf-8", errors="replace")
    dir_idx = fe.dir_index

    if version >= 5:
        raw = include_dirs[dir_idx].decode("utf-8", errors="replace")
        base = raw if os.path.isabs(raw) else os.path.join(comp_dir, raw)
    elif dir_idx == 0:
        base = comp_dir
    else:
        raw = include_dirs[dir_idx - 1].decode("utf-8", errors="replace")
        base = raw if os.path.isabs(raw) else os.path.join(comp_dir, raw)

    full = name if os.path.isabs(name) else os.path.join(base, name)
    return os.path.realpath(full)


# Sources are measured repeatedly -- once per routine that contributed lines
# to them -- so keep the most recent line tables around.
@lru_cache(maxsize=128)
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
