#!/usr/bin/env python3

# Copyright (c) 2026 The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""
doxygen_group_check.py - Report Doxygen entities not assigned to any group.

Optionally runs Doxygen with XML output enabled, then parses the resulting
XML to find explicitly documented entities (functions, typedefs, enums,
structs, unions, macros — those carrying a ``/** */`` or ``/*! */`` Doxygen
comment) that do not belong to any Doxygen group (``@defgroup`` /
``@addtogroup``).  Entities with no explicit Doxygen comment are ignored.
Emits a Markdown report organised by source file.

A **super-error** (⛔) is raised for any file where *all* of its documented
entities are ungrouped, meaning the file has zero group coverage.

Usage
-----
Use an already-built XML tree::

    python3 scripts/doxygen_group_check.py --xml-dir build/doxygen/xml

Run Doxygen first, then analyse (requires ``--zephyr-base``)::

    python3 scripts/doxygen_group_check.py \\
        --run-doxygen \\
        --doxyfile doc/zephyr.doxyfile.in \\
        --zephyr-base /path/to/zephyr \\
        --xml-dir /tmp/doxy_out/xml

Write the report to a file instead of stdout::

    python3 scripts/doxygen_group_check.py --xml-dir build/doxygen/xml \\
        --output ungrouped_report.md

Exit codes
----------
0 – no ungrouped entities found
1 – at least one ungrouped entity found (non-zero even for warnings only)
2 – usage / environment error
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# memberdef kinds that are treated as first-class API entities
MEMBER_KINDS: frozenset = frozenset({"function", "typedef", "enum", "define", "variable"})

# compound kinds (structs / unions / classes) tracked as top-level entities
COMPOUND_KINDS: frozenset = frozenset({"struct", "union", "class"})

# sectiondef kinds to skip (internal / implementation detail)
SKIP_SECTION_KINDS: frozenset = frozenset(
    {
        "private-attrib",
        "private-func",
        "private-static-attrib",
        "private-static-func",
        "private-type",
        "protected-attrib",
        "protected-func",
        "protected-static-attrib",
        "protected-static-func",
        "protected-type",
        "detail",
    }
)


# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------


@dataclass
class Entity:
    """A single documented entity (function, typedef, struct, …)."""

    refid: str
    name: str
    kind: str
    file: str
    line: int
    brief: str = ""


@dataclass
class FileReport:
    """Per-source-file grouping coverage summary."""

    path: str
    total: int = 0
    ungrouped: List[Entity] = field(default_factory=list)

    @property
    def is_super_error(self) -> bool:
        """True when *every* entity in the file is ungrouped."""
        return self.total > 0 and len(self.ungrouped) == self.total

    @property
    def coverage_ok(self) -> bool:
        return len(self.ungrouped) == 0


# ---------------------------------------------------------------------------
# Doxygen runner
# ---------------------------------------------------------------------------


def run_doxygen(
    doxygen_exe: str,
    doxyfile_in: str,
    out_dir: Path,
    zephyr_base: Path,
) -> None:
    """Run Doxygen with XML output forced on and HTML/LaTeX off.

    The ``doxyfile_in`` may contain ``@ZEPHYR_BASE@`` placeholders which are
    substituted before running.

    Args:
        doxygen_exe: Path to (or name of) the Doxygen executable.
        doxyfile_in: Path to the Doxyfile (may be a ``.in`` template).
        out_dir: Directory that will receive the Doxygen ``xml/`` tree.
        zephyr_base: Zephyr repository root used to expand ``@ZEPHYR_BASE@``.
    """

    with open(doxyfile_in, encoding="utf-8") as fh:
        content = fh.read()

    # Expand common template placeholders
    content = content.replace("@ZEPHYR_BASE@", str(zephyr_base))
    # Expand any remaining @VAR@ tokens to the Zephyr base as a safe fallback
    content = re.sub(r"@[A-Z0-9_]+@", str(zephyr_base), content)

    # Force settings required by this script
    def _override(key: str, value: str, text: str) -> str:
        pattern = rf"^\s*{re.escape(key)}\s*=.*$"
        replacement = f"{key} = {value}"
        new_text, count = re.subn(pattern, replacement, text, flags=re.MULTILINE)
        if count == 0:
            new_text = text + f"\n{replacement}\n"
        return new_text

    out_dir.mkdir(parents=True, exist_ok=True)
    content = _override("OUTPUT_DIRECTORY", str(out_dir), content)
    content = _override("GENERATE_XML", "YES", content)
    content = _override("XML_OUTPUT", "xml", content)
    content = _override("GENERATE_HTML", "NO", content)
    content = _override("GENERATE_LATEX", "NO", content)
    content = _override("QUIET", "YES", content)

    with tempfile.NamedTemporaryFile(
        "w", suffix=".doxyfile", delete=False, encoding="utf-8"
    ) as tmp:
        tmp.write(content)
        tmp_path = tmp.name

    try:
        result = subprocess.run(
            [doxygen_exe, tmp_path],
            capture_output=True,
            text=True,
        )
    finally:
        os.unlink(tmp_path)

    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        sys.exit(f"Doxygen exited with code {result.returncode}")

    print(f"Doxygen XML written to: {out_dir / 'xml'}", file=sys.stderr)


# ---------------------------------------------------------------------------
# XML parsing helpers
# ---------------------------------------------------------------------------


def _brief_text(element: Optional[ET.Element]) -> str:
    """Extract plain text from a Doxygen ``<briefdescription>`` element."""
    if element is None:
        return ""
    parts: List[str] = []
    for para in element.findall("para"):
        text = "".join(para.itertext()).strip()
        if text:
            parts.append(text)
    return " ".join(parts)


def _has_explicit_doc(element: ET.Element) -> bool:
    """Return True if *element* carries an explicit Doxygen documentation comment.

    Doxygen emits XML for every entity it finds (especially with
    ``EXTRACT_ALL = YES``), even those with no ``/** */`` or ``/*! */``
    comment.  Undocumented entities have completely empty
    ``<briefdescription>`` and ``<detaileddescription>`` elements.  This
    function returns ``True`` only when at least one of those two elements
    contains non-whitespace text, indicating that a real Doxygen comment was
    present in the source.

    Args:
        element: A ``<memberdef>`` or ``<compounddef>`` XML element.

    Returns:
        ``True`` if the entity has an explicit Doxygen comment, ``False``
        otherwise.
    """
    for tag in ("briefdescription", "detaileddescription"):
        child = element.find(tag)
        if child is not None and "".join(child.itertext()).strip():
            return True
    return False


def parse_index(xml_dir: Path) -> Dict[str, str]:
    """Return ``{refid: kind}`` for every compound in ``index.xml``.

    Args:
        xml_dir: Directory containing the Doxygen XML output.

    Returns:
        Mapping from compound refid to its kind string.
    """
    index_path = xml_dir / "index.xml"
    if not index_path.exists():
        sys.exit(f"Cannot find {index_path}. Did Doxygen run successfully?")

    tree = ET.parse(index_path)
    return {
        c.get("refid", ""): c.get("kind", "")
        for c in tree.getroot().findall("compound")
        if c.get("refid")
    }


def collect_grouped_locations(
    xml_dir: Path,
    compounds: Dict[str, str],
) -> Tuple[Set[Tuple[str, int]], Set[str]]:
    """Scan every group compound and collect membership information.

    Returns:
        A pair ``(member_locations, compound_refids)`` where:

        - *member_locations* is a set of ``(file_path, line)`` tuples for
          every ``memberdef`` that appears inside at least one group.
        - *compound_refids* is a set of refids for every ``innerclass`` /
          ``innernamespace`` listed inside at least one group (covers
          struct / union / class entities).
    """
    member_locations: Set[Tuple[str, int]] = set()
    compound_refids: Set[str] = set()

    for refid, kind in compounds.items():
        if kind != "group":
            continue

        xml_file = xml_dir / f"{refid}.xml"
        if not xml_file.exists():
            continue

        root = ET.parse(xml_file).getroot()
        for cdef in root.findall("compounddef"):
            # Inner compounds (structs, unions, nested namespaces, …)
            for inner in cdef.findall("innerclass") + cdef.findall("innernamespace"):
                inner_refid = inner.get("refid")
                if inner_refid:
                    compound_refids.add(inner_refid)

            # Member definitions
            for section in cdef.findall("sectiondef"):
                for mdef in section.findall("memberdef"):
                    loc = mdef.find("location")
                    if loc is None:
                        continue
                    fpath = loc.get("file", "")
                    line = loc.get("line")
                    if fpath and line:
                        try:
                            member_locations.add((fpath, int(line)))
                        except ValueError:
                            pass

    return member_locations, compound_refids


def collect_file_member_entities(
    xml_dir: Path,
    compounds: Dict[str, str],
) -> Dict[str, List[Entity]]:
    """Collect member entities (functions, typedefs, enums, …) from file compounds.

    Args:
        xml_dir: Directory containing the Doxygen XML output.
        compounds: Mapping from refid to kind (from :func:`parse_index`).

    Returns:
        Mapping from source file path to the list of :class:`Entity` objects
        found in that file's compound XML.
    """
    by_file: Dict[str, List[Entity]] = defaultdict(list)

    for refid, kind in compounds.items():
        if kind != "file":
            continue

        xml_file = xml_dir / f"{refid}.xml"
        if not xml_file.exists():
            continue

        root = ET.parse(xml_file).getroot()
        for cdef in root.findall("compounddef"):
            for section in cdef.findall("sectiondef"):
                if section.get("kind", "") in SKIP_SECTION_KINDS:
                    continue

                for mdef in section.findall("memberdef"):
                    m_kind = mdef.get("kind", "")
                    if m_kind not in MEMBER_KINDS:
                        continue

                    if not _has_explicit_doc(mdef):
                        continue

                    loc = mdef.find("location")
                    if loc is None:
                        continue

                    fpath = loc.get("file", "")
                    line_str = loc.get("line")
                    if not fpath or not line_str:
                        continue

                    try:
                        line = int(line_str)
                    except ValueError:
                        continue

                    entity = Entity(
                        refid=mdef.get("id", ""),
                        name=mdef.findtext("name", ""),
                        kind=m_kind,
                        file=fpath,
                        line=line,
                        brief=_brief_text(mdef.find("briefdescription")),
                    )
                    by_file[fpath].append(entity)

    return by_file


def collect_compound_entities(
    xml_dir: Path,
    compounds: Dict[str, str],
) -> Dict[str, List[Entity]]:
    """Collect struct / union / class compounds and map them to their source file.

    Args:
        xml_dir: Directory containing the Doxygen XML output.
        compounds: Mapping from refid to kind (from :func:`parse_index`).

    Returns:
        Mapping from source file path to the list of compound :class:`Entity`
        objects (structs, unions, classes) defined in that file.
    """
    by_file: Dict[str, List[Entity]] = defaultdict(list)

    for refid, kind in compounds.items():
        if kind not in COMPOUND_KINDS:
            continue

        xml_file = xml_dir / f"{refid}.xml"
        if not xml_file.exists():
            continue

        root = ET.parse(xml_file).getroot()
        for cdef in root.findall("compounddef"):
            if not _has_explicit_doc(cdef):
                continue

            loc = cdef.find("location")
            if loc is None:
                continue

            fpath = loc.get("file", "")
            if not fpath:
                continue

            try:
                line = int(loc.get("line", "0"))
            except ValueError:
                line = 0

            entity = Entity(
                refid=refid,
                name=cdef.findtext("compoundname", ""),
                kind=kind,
                file=fpath,
                line=line,
                brief=_brief_text(cdef.find("briefdescription")),
            )
            by_file[fpath].append(entity)

    return by_file


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------


def analyse(
    xml_dir: Path,
    zephyr_base: Optional[Path] = None,
) -> List[FileReport]:
    """Parse Doxygen XML and build per-file coverage reports.

    Args:
        xml_dir: Directory containing the Doxygen ``xml/`` output.
        zephyr_base: When provided, file paths are reported relative to this
            directory for readability.

    Returns:
        List of :class:`FileReport` objects (one per source file that has at
        least one ungrouped entity), sorted by path.
    """
    compounds = parse_index(xml_dir)
    grouped_locs, grouped_refids = collect_grouped_locations(xml_dir, compounds)

    # Collect all entities, merging members and compound types
    all_by_file: Dict[str, List[Entity]] = defaultdict(list)
    for fpath, entities in collect_file_member_entities(xml_dir, compounds).items():
        all_by_file[fpath].extend(entities)
    for fpath, entities in collect_compound_entities(xml_dir, compounds).items():
        all_by_file[fpath].extend(entities)

    # Build reports
    reports: List[FileReport] = []
    for fpath, entities in sorted(all_by_file.items()):
        display_path = fpath
        if zephyr_base:
            try:
                display_path = str(Path(fpath).relative_to(zephyr_base))
            except ValueError:
                pass

        # Deduplicate by (kind, name, line) – Doxygen can emit duplicates when
        # a symbol appears in both a namespace and file compound
        seen: Set[Tuple[str, str, int]] = set()
        unique_entities: List[Entity] = []
        for e in sorted(entities, key=lambda x: x.line):
            key = (e.kind, e.name, e.line)
            if key not in seen:
                seen.add(key)
                unique_entities.append(e)

        ungrouped: List[Entity] = []
        for e in unique_entities:
            in_group = (e.file, e.line) in grouped_locs or e.refid in grouped_refids
            if not in_group:
                ungrouped.append(e)

        report = FileReport(
            path=display_path,
            total=len(unique_entities),
            ungrouped=ungrouped,
        )
        if not report.coverage_ok:
            reports.append(report)

    return reports


# ---------------------------------------------------------------------------
# Report generation
# ---------------------------------------------------------------------------

_KIND_LABEL: Dict[str, str] = {
    "function": "function",
    "typedef": "typedef",
    "enum": "enum",
    "define": "macro",
    "variable": "variable",
    "struct": "struct",
    "union": "union",
    "class": "class",
}


def _entity_table(entities: List[Entity]) -> str:
    """Render a Markdown table for a list of entities."""
    lines = ["| Entity | Kind | Line | Brief |", "|--------|------|------|-------|"]
    for e in entities:
        label = _KIND_LABEL.get(e.kind, e.kind)
        brief = e.brief.replace("|", "\\|") if e.brief else "—"
        lines.append(f"| `{e.name}` | {label} | {e.line} | {brief} |")
    return "\n".join(lines)


def generate_report(
    reports: List[FileReport],
    xml_dir: Path,
    zephyr_base: Optional[Path],
) -> str:
    """Render the full Markdown coverage report.

    Args:
        reports: Per-file report objects produced by :func:`analyse`.
        xml_dir: XML directory used for the analysis (shown in the report).
        zephyr_base: Zephyr base directory (shown in the report header).

    Returns:
        The complete report as a Markdown string.
    """
    super_errors = [r for r in reports if r.is_super_error]
    partial = [r for r in reports if not r.is_super_error]

    total_ungrouped = sum(len(r.ungrouped) for r in reports)
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")

    lines: List[str] = []
    lines.append("# Doxygen Group Coverage Report\n")
    lines.append(f"**Generated:** {now}  ")
    if zephyr_base:
        lines.append(f"**Zephyr base:** `{zephyr_base}`  ")
    lines.append(f"**XML directory:** `{xml_dir}`\n")

    lines.append("## Summary\n")
    lines.append("| Metric | Count |")
    lines.append("|--------|-------|")
    lines.append(f"| Files with ungrouped entities | {len(reports)} |")
    lines.append(
        f"| ⛔ Super-errors (entire file ungrouped) | {len(super_errors)} |"
    )
    lines.append(f"| Files with partial coverage issues | {len(partial)} |")
    lines.append(f"| Total ungrouped entities | {total_ungrouped} |")
    lines.append("")

    if not reports:
        lines.append("> ✅ All documented entities are assigned to a group.\n")
        return "\n".join(lines)

    # -----------------------------------------------------------------------
    # Super-errors
    # -----------------------------------------------------------------------
    if super_errors:
        lines.append("---\n")
        lines.append("## ⛔ Super-errors: Files with Zero Group Coverage\n")
        lines.append(
            "> Every documented entity in these files is ungrouped.  "
            "Add `@ingroup` / `@addtogroup` annotations or assign the file "
            "to a group.\n"
        )
        for r in super_errors:
            lines.append(f"### `{r.path}`\n")
            lines.append(
                f"All **{r.total}** documented "
                f"{'entity' if r.total == 1 else 'entities'} "
                f"in this file {'is' if r.total == 1 else 'are'} ungrouped.\n"
            )
            lines.append(_entity_table(r.ungrouped))
            lines.append("")

    # -----------------------------------------------------------------------
    # Partial coverage issues
    # -----------------------------------------------------------------------
    if partial:
        lines.append("---\n")
        lines.append("## ⚠️ Files with Partial Group Coverage\n")
        for r in partial:
            lines.append(f"### `{r.path}`\n")
            covered = r.total - len(r.ungrouped)
            lines.append(
                f"{len(r.ungrouped)} of {r.total} entities ungrouped "
                f"({covered} / {r.total} have group coverage).\n"
            )
            lines.append(_entity_table(r.ungrouped))
            lines.append("")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )

    parser.add_argument(
        "--xml-dir",
        metavar="DIR",
        help=(
            "Path to the Doxygen XML output directory (the directory that "
            "contains index.xml). Required unless --run-doxygen is given."
        ),
    )

    run_group = parser.add_argument_group("Doxygen runner (optional)")
    run_group.add_argument(
        "--run-doxygen",
        action="store_true",
        help="Run Doxygen before analysing. Requires --doxyfile.",
    )
    run_group.add_argument(
        "--doxyfile",
        metavar="FILE",
        help=(
            "Path to the Doxyfile (or .doxyfile.in template). "
            "Defaults to doc/zephyr.doxyfile.in relative to --zephyr-base."
        ),
    )
    run_group.add_argument(
        "--doxygen-exe",
        metavar="EXE",
        default="doxygen",
        help="Doxygen executable (default: %(default)s).",
    )
    run_group.add_argument(
        "--doxygen-out",
        metavar="DIR",
        help=(
            "Output directory for the Doxygen build. The XML tree will be "
            "written to <DIR>/xml. Defaults to a temporary directory."
        ),
    )

    parser.add_argument(
        "--zephyr-base",
        metavar="DIR",
        help=(
            "Zephyr repository root. Used to expand @ZEPHYR_BASE@ in the "
            "Doxyfile template and to produce relative paths in the report. "
            "Defaults to ZEPHYR_BASE env var or the parent of this script."
        ),
    )
    parser.add_argument(
        "--output",
        "-o",
        metavar="FILE",
        help="Write the Markdown report to FILE instead of stdout.",
    )
    parser.add_argument(
        "--no-exit-code",
        action="store_true",
        help="Always exit with code 0 (useful in CI where the report is informational).",
    )

    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = _parse_args(argv)

    # Resolve Zephyr base
    zephyr_base: Optional[Path] = None
    if args.zephyr_base:
        zephyr_base = Path(args.zephyr_base).resolve()
    elif "ZEPHYR_BASE" in os.environ:
        zephyr_base = Path(os.environ["ZEPHYR_BASE"]).resolve()
    else:
        # Fall back to the parent of this script
        zephyr_base = Path(__file__).resolve().parent.parent

    # Determine XML directory
    xml_dir: Optional[Path] = None
    _tmp_dir = None

    if args.run_doxygen:
        doxyfile = args.doxyfile
        if not doxyfile:
            if zephyr_base:
                doxyfile = str(zephyr_base / "doc" / "zephyr.doxyfile.in")
            else:
                sys.exit("--doxyfile is required when --run-doxygen is given")

        if args.doxygen_out:
            doxy_out = Path(args.doxygen_out).resolve()
        else:
            _tmp_dir = tempfile.mkdtemp(prefix="zephyr_doxy_")
            doxy_out = Path(_tmp_dir)

        print(f"Running Doxygen → {doxy_out} …", file=sys.stderr)
        run_doxygen(args.doxygen_exe, doxyfile, doxy_out, zephyr_base)
        xml_dir = doxy_out / "xml"

    elif args.xml_dir:
        xml_dir = Path(args.xml_dir).resolve()
    else:
        sys.exit(
            "Provide --xml-dir pointing at the Doxygen XML output directory, "
            "or use --run-doxygen to build first."
        )

    print(f"Analysing XML in {xml_dir} …", file=sys.stderr)
    reports = analyse(xml_dir, zephyr_base)

    report_text = generate_report(reports, xml_dir, zephyr_base)

    if args.output:
        Path(args.output).write_text(report_text, encoding="utf-8")
        print(f"Report written to {args.output}", file=sys.stderr)
    else:
        print(report_text)

    # Clean up temporary directory if we created one
    if _tmp_dir:
        import shutil

        shutil.rmtree(_tmp_dir, ignore_errors=True)

    if args.no_exit_code:
        return 0
    return 1 if reports else 0


if __name__ == "__main__":
    sys.exit(main())
