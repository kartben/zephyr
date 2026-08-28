#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0
"""Tests for zspdx.dwarf, the used-source extractor.

The DWARF reading itself needs a linked image, so what is covered here is the
logic that turns line entries into routines: which routine owns a line, how
line numbers become byte ranges, and which declaration site is worth recording.
"""

import os
import sys

import pytest

ZEPHYR_BASE = os.getenv("ZEPHYR_BASE")
sys.path.insert(0, os.path.join(ZEPHYR_BASE, "scripts/pylib"))

from zspdx.dwarf import (  # noqa: E402
    Routine,
    SourceRange,
    _lines_to_ranges,
    _owning_routine,
    _pick_declaration,
    _scope_index,
    _textual_owner,
    line_byte_range,
)

MAIN = "/src/main.c"
UTIL = "/src/util.h"

# Three routines: two written in main.c, one static inline in a header.
BOOT = ("boot", MAIN, 10)
RUN = ("run", MAIN, 40)
CLAMP = ("clamp", UTIL, 7)


def definitions(*identities):
    """Build the (sites, ladder) pair _textual_owner expects."""
    sites = {}
    for identity in identities:
        sites.setdefault(identity[1], {})[identity[2]] = identity
    return sites, {path: sorted(by_line) for path, by_line in sites.items()}


def owner(address, line, path, scopes, defs):
    lows, reach = _scope_index(scopes)
    return _owning_routine(address, line, path, scopes, lows, reach, defs)


class TestTextualOwner:
    """Which routine a line sits in is a property of the source alone."""

    def test_picks_the_last_routine_defined_at_or_above_the_line(self):
        defs = definitions(BOOT, RUN)
        assert _textual_owner(MAIN, 10, defs) == BOOT
        assert _textual_owner(MAIN, 25, defs) == BOOT
        assert _textual_owner(MAIN, 40, defs) == RUN
        assert _textual_owner(MAIN, 99, defs) == RUN

    def test_no_owner_above_the_first_routine(self):
        assert _textual_owner(MAIN, 3, definitions(BOOT, RUN)) is None

    def test_no_owner_in_a_file_holding_no_routines(self):
        assert _textual_owner(UTIL, 12, definitions(BOOT)) is None


class TestOwningRoutine:
    def test_line_inside_a_routine_belongs_to_it(self):
        scopes = [(0x100, 0x200, 0, BOOT)]
        assert owner(0x110, 12, MAIN, scopes, definitions(BOOT)) == BOOT

    def test_inlined_callee_does_not_absorb_its_call_site(self):
        # run() at 0x100 inlines clamp(); the inlined body covers 0x140-0x160.
        # Line 45 of main.c is the call site: it is run()'s line, even though
        # its address falls inside the range DWARF gave the inlined callee.
        scopes = [(0x100, 0x200, 0, RUN), (0x140, 0x160, 1, CLAMP)]
        defs = definitions(RUN, CLAMP)
        assert owner(0x150, 45, MAIN, scopes, defs) == RUN
        # ...while the inlined body's own lines stay with the callee.
        assert owner(0x150, 8, UTIL, scopes, defs) == CLAMP

    def test_caller_does_not_absorb_a_sibling_routines_lines(self):
        # The optimiser can leave boot()'s code inside the range given to
        # run(); the source structure still says line 12 is boot's.
        scopes = [(0x100, 0x200, 0, RUN)]
        assert owner(0x150, 12, MAIN, scopes, definitions(BOOT, RUN)) == BOOT

    def test_macro_lines_go_to_the_routine_that_expanded_them(self):
        # util.h line 30 is a macro body, not a routine: no routine is defined
        # at or above it in that file, so it belongs to its expander.
        scopes = [(0x100, 0x200, 0, RUN)]
        assert owner(0x150, 30, UTIL, scopes, definitions(RUN)) == RUN

    def test_line_outside_every_scope_falls_back_to_the_source_structure(self):
        # No DIE covers this address -- the optimiser can leave a routine's own
        # code outside the range DWARF gives it -- but the source still knows.
        scopes = [(0x100, 0x200, 0, RUN)]
        assert owner(0x900, 45, MAIN, scopes, definitions(RUN)) == RUN

    def test_line_belonging_to_no_routine_at_all_is_unattributed(self):
        # Hand-written assembly: line entries but no subprogram DIEs anywhere.
        scopes = [(0x100, 0x200, 0, RUN)]
        assert owner(0x900, 12, "/src/vectors.S", scopes, definitions(RUN)) is None

    def test_innermost_of_nested_inlines_wins(self):
        inner = ("inner", UTIL, 20)
        scopes = [
            (0x100, 0x200, 0, RUN),
            (0x140, 0x180, 1, CLAMP),
            (0x150, 0x160, 2, inner),
        ]
        defs = definitions(RUN, CLAMP, inner)
        assert owner(0x155, 21, UTIL, scopes, defs) == inner
        assert owner(0x170, 8, UTIL, scopes, defs) == CLAMP


class TestPickDeclaration:
    def test_prefers_a_header_over_a_forward_declaration_in_a_c_file(self):
        sites = {(UTIL, 3), ("/src/other.c", 9)}
        assert _pick_declaration(sites, MAIN) == (UTIL, 3)

    def test_ignores_a_declaration_in_the_defining_file(self):
        # A static inline declares itself where it is defined.
        assert _pick_declaration({(UTIL, 7)}, UTIL) is None

    def test_ignores_non_header_declarations(self):
        assert _pick_declaration({("/src/other.c", 9)}, MAIN) is None

    def test_no_sites_at_all(self):
        assert _pick_declaration(None, MAIN) is None
        assert _pick_declaration(set(), MAIN) is None

    def test_picks_deterministically_among_several_headers(self):
        sites = {("/src/b.h", 5), ("/src/a.h", 8)}
        assert _pick_declaration(sites, MAIN) == ("/src/a.h", 8)


class TestLineRanges:
    @pytest.fixture
    def source(self, tmp_path):
        # 5 lines of 4, 6, 4, 6 and 4 bytes, each followed by a newline.
        path = tmp_path / "sample.c"
        path.write_text("aaaa\nbbbbbb\ncccc\ndddddd\neeee\n")
        return str(path)

    def test_consecutive_lines_fold_into_one_range(self, source):
        ranges = _lines_to_ranges(source, [1, 2, 3])
        assert [(r.start_line, r.end_line) for r in ranges] == [(1, 3)]

    def test_gaps_split_ranges(self, source):
        ranges = _lines_to_ranges(source, [1, 2, 4, 5])
        assert [(r.start_line, r.end_line) for r in ranges] == [(1, 2), (4, 5)]

    def test_byte_offsets_span_the_lines_without_the_final_newline(self, source):
        (only,) = _lines_to_ranges(source, [2, 3])
        assert (only.start_byte, only.end_byte) == (5, 16)
        with open(source, "rb") as f:
            assert f.read()[only.start_byte : only.end_byte] == b"bbbbbb\ncccc"

    def test_no_lines_no_ranges(self, source):
        assert _lines_to_ranges(source, []) == []

    def test_unreadable_source_yields_no_ranges(self, tmp_path):
        assert _lines_to_ranges(str(tmp_path / "missing.c"), [1, 2]) == []

    def test_line_byte_range_matches_the_file(self, source):
        assert line_byte_range(source, 1, 1) == (0, 4)
        assert line_byte_range(source, 1, 5) == (0, 28)

    def test_line_byte_range_clamps_past_the_end(self, source):
        assert line_byte_range(source, 5, 400) == line_byte_range(source, 5, 5)

    def test_a_trailing_newline_does_not_invent_a_line(self, tmp_path):
        terminated = tmp_path / "terminated.c"
        terminated.write_text("aaaa\nbbbb\n")
        unterminated = tmp_path / "unterminated.c"
        unterminated.write_text("aaaa\nbbbb")
        assert line_byte_range(str(terminated), 1, 400) == (0, 9)
        assert line_byte_range(str(unterminated), 1, 400) == (0, 9)

    def test_line_byte_range_of_an_unreadable_file(self, tmp_path):
        assert line_byte_range(str(tmp_path / "missing.c"), 1, 2) is None


class TestRoutineExtent:
    def test_extent_covers_only_the_routines_own_file(self):
        routine = Routine(
            name="run",
            decl_file=MAIN,
            decl_line=40,
            ranges=[
                SourceRange(MAIN, 41, 43, 0, 10),
                SourceRange(UTIL, 30, 30, 0, 5),
                SourceRange(MAIN, 47, 48, 20, 30),
            ],
        )
        assert routine.line_extent == (41, 48)

    def test_no_extent_when_the_routine_only_pulled_in_other_files(self):
        routine = Routine(
            name="run",
            decl_file=MAIN,
            decl_line=40,
            ranges=[SourceRange(UTIL, 30, 30, 0, 5)],
        )
        assert routine.line_extent is None
