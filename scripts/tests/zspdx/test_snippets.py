#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0
"""Tests for the snippet hierarchy zspdx.sbom builds out of extracted routines."""

import os
import sys

import pytest

ZEPHYR_BASE = os.getenv("ZEPHYR_BASE")
sys.path.insert(0, os.path.join(ZEPHYR_BASE, "scripts/pylib"))

from zspdx.dwarf import Routine, SourceRange  # noqa: E402
from zspdx.model import (  # noqa: E402
    SBOMComponent,
    SBOMDocument,
    SBOMFile,
    SBOMGraph,
    SnippetKind,
)
from zspdx.sbom import _extract_snippets  # noqa: E402


@pytest.fixture
def sources(tmp_path):
    """A .c defining two routines and a .h declaring one of them."""
    main = tmp_path / "main.c"
    main.write_text("\n".join(f"line {n:02d}" for n in range(1, 21)) + "\n")
    header = tmp_path / "util.h"
    header.write_text("\n".join(f"decl {n:02d}" for n in range(1, 11)) + "\n")
    return str(main), str(header)


@pytest.fixture
def graph(sources):
    graph = SBOMGraph()
    graph.namespace_prefix = "http://example.invalid/spdx"
    graph.add_document(SBOMDocument(name="app"))
    component = SBOMComponent(name="app")
    graph.add_component(component, "app")
    for path in sources:
        graph.add_file(SBOMFile(path=path, relative_path=os.path.basename(path)), component)
    return graph


def by_kind(graph, kind):
    return [s for s in graph.snippets if s.kind is kind]


def make_routine(main, header, **kwargs):
    return Routine(
        name=kwargs.pop("name", "run"),
        decl_file=main,
        decl_line=kwargs.pop("decl_line", 5),
        ranges=kwargs.pop("ranges", [SourceRange(main, 6, 8, 0, 0)]),
        **kwargs,
    )


def resolve_bytes(routine_ranges, main):
    """Fill in real byte offsets for ranges in *main* (1-based, 8-byte lines)."""
    for r in routine_ranges:
        if r.path == main:
            r.start_byte = (r.start_line - 1) * 8
            r.end_byte = r.end_line * 8 - 1


class TestRoutineSnippets:
    def test_one_snippet_per_routine_by_default(self, graph, sources):
        main, header = sources
        routines = [
            make_routine(main, header, name="boot", decl_line=2),
            make_routine(
                main, header, name="run", decl_line=10, ranges=[SourceRange(main, 11, 13, 80, 103)]
            ),
        ]
        _extract_snippets(graph, "image.elf", routines, with_lines=False)

        assert len(by_kind(graph, SnippetKind.ROUTINE)) == 2
        assert by_kind(graph, SnippetKind.RANGE) == []
        assert {s.routine for s in graph.snippets} == {"boot", "run"}

    def test_routine_snippet_spans_what_the_routine_contributed(self, graph, sources):
        main, _ = sources
        ranges = [SourceRange(main, 6, 7, 0, 0), SourceRange(main, 12, 14, 0, 0)]
        resolve_bytes(ranges, main)
        _extract_snippets(
            graph, "image.elf", [make_routine(main, None, ranges=ranges)], with_lines=False
        )

        (routine,) = by_kind(graph, SnippetKind.ROUTINE)
        assert routine.line_range == (6, 14)
        assert routine.byte_range == (40, 111)
        assert routine.name == "run@main.c"

    def test_line_ranges_are_opt_in_and_hang_off_their_routine(self, graph, sources):
        main, _ = sources
        ranges = [SourceRange(main, 6, 7, 40, 55), SourceRange(main, 12, 14, 88, 111)]
        _extract_snippets(
            graph, "image.elf", [make_routine(main, None, ranges=ranges)], with_lines=True
        )

        (routine,) = by_kind(graph, SnippetKind.ROUTINE)
        line_ranges = by_kind(graph, SnippetKind.RANGE)
        assert len(line_ranges) == 2
        assert all(r.parent is routine for r in line_ranges)
        assert [r.line_range for r in line_ranges] == [(6, 7), (12, 14)]

    def test_a_range_can_live_in_another_file(self, graph, sources):
        # A macro in util.h expanded inside run(): the text is the header's.
        main, header = sources
        ranges = [SourceRange(main, 6, 6, 40, 47), SourceRange(header, 3, 3, 16, 23)]
        _extract_snippets(
            graph, "image.elf", [make_routine(main, None, ranges=ranges)], with_lines=True
        )

        from_header = [r for r in by_kind(graph, SnippetKind.RANGE) if r.spdx_file.path == header]
        assert len(from_header) == 1
        assert from_header[0].routine == "run"
        assert from_header[0].parent.spdx_file.path == main

    def test_routines_outside_the_tracked_files_are_skipped(self, graph, sources):
        main, _ = sources
        stray = Routine(
            name="stray",
            decl_file="/nowhere/other.c",
            decl_line=1,
            ranges=[SourceRange("/nowhere/other.c", 1, 2, 0, 10)],
        )
        _extract_snippets(graph, "image.elf", [stray], with_lines=False)
        assert graph.snippets == []

    def test_inlined_only_is_carried_through(self, graph, sources):
        main, _ = sources
        routine = make_routine(main, None, inlined_only=True)
        _extract_snippets(graph, "image.elf", [routine], with_lines=False)
        assert by_kind(graph, SnippetKind.ROUTINE)[0].inlined_only is True


class TestDeclarationSnippets:
    def test_header_prototype_becomes_a_declaration_the_routine_points_at(self, graph, sources):
        main, header = sources
        routine = make_routine(main, header, declared_in=header, declared_line=4)
        _extract_snippets(graph, "image.elf", [routine], with_lines=False)

        (declaration,) = by_kind(graph, SnippetKind.DECLARATION)
        assert declaration.spdx_file.path == header
        assert declaration.line_range == (4, 4)
        assert by_kind(graph, SnippetKind.ROUTINE)[0].declaration is declaration

    def test_one_declaration_snippet_per_site(self, graph, sources):
        main, header = sources
        # The same prototype reached through two routine records.
        routines = [
            make_routine(
                main, header, name="run", decl_line=5, declared_in=header, declared_line=4
            ),
            make_routine(
                main,
                header,
                name="run",
                decl_line=5,
                ranges=[SourceRange(main, 9, 9, 64, 71)],
                declared_in=header,
                declared_line=4,
            ),
        ]
        _extract_snippets(graph, "image.elf", routines, with_lines=False)

        declarations = by_kind(graph, SnippetKind.DECLARATION)
        assert len(declarations) == 1
        assert all(r.declaration is declarations[0] for r in by_kind(graph, SnippetKind.ROUTINE))

    def test_untracked_header_yields_no_declaration(self, graph, sources):
        main, _ = sources
        routine = make_routine(main, None, declared_in="/nowhere/other.h", declared_line=4)
        _extract_snippets(graph, "image.elf", [routine], with_lines=False)

        assert by_kind(graph, SnippetKind.DECLARATION) == []
        assert by_kind(graph, SnippetKind.ROUTINE)[0].declaration is None
