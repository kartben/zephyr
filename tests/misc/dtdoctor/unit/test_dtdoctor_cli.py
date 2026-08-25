#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Tests for the analyzer CLI symbol/ordinal resolution and exit codes."""

import dtdoctor_analyzer
from conftest import DTS_DISABLED_FULL, DTS_MACROS, ord_symbol


def test_non_device_symbol_silent_rc1(run_analyzer):
    # The wrapper only ever passes __device_dts_ord_* symbols; anything else is
    # rejected silently, before the pickle is even opened
    rc, out, err = run_analyzer("does-not-matter.pickle", "some_other_symbol")
    assert rc == 1
    assert out == ""
    assert err == ""


def test_symbol_embedded_in_text_accepted(make_edt, make_pickle, run_analyzer):
    edt, _ = make_edt(DTS_DISABLED_FULL)
    symbol = ord_symbol(edt, "foo_dev")
    rc, out, _ = run_analyzer(make_pickle(edt), f"`{symbol}' referenced in section .text")
    assert rc == 0
    assert "DT Doctor" in out


def test_unknown_ordinal_reports_error(make_edt, make_pickle, run_analyzer):
    edt, _ = make_edt(DTS_DISABLED_FULL)
    missing = max(n.dep_ordinal for n in edt.nodes) + 1000
    rc, out, err = run_analyzer(make_pickle(edt), f"__device_dts_ord_{missing}")
    assert rc == 1
    assert out == ""
    assert f"Ordinal {missing} not found" in err


def test_unclassifiable_dt_macro_silent_rc1(make_edt, make_pickle, run_analyzer):
    # The wrapper casts a wider net than the analyzer can make sense of; anything it cannot
    # explain is dropped without adding noise to an already failing build
    edt, _ = make_edt(DTS_MACROS)
    rc, out, err = run_analyzer(make_pickle(edt), "DT_N_INST_nonsense")
    assert rc == 1
    assert out == ""
    assert err == ""


def test_device_symbol_prefix_is_stripped():
    # DEVICE_DT_GET() pastes the node identifier onto the device symbol, so the macro to
    # diagnose is the tail rather than the whole symbol
    symbol = "__device_dts_ord_DT_N_NODELABEL_nope_ORD"
    assert dtdoctor_analyzer.dt_macro(symbol) == "DT_N_NODELABEL_nope_ORD"


def test_non_dt_symbol_has_no_macro():
    assert dtdoctor_analyzer.dt_macro("some_other_symbol") is None
