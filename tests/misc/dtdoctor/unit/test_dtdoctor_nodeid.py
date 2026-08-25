#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Tests for the diagnosis of node identifiers that name no node."""

import dtdoctor_analyzer
from conftest import DTS_MACROS, SERIAL0_PATH_ID, device_symbol


def diagnose(edt, symbol):
    """Run the same symbol -> macro -> diagnosis path main() takes."""
    return "\n".join(dtdoctor_analyzer.diagnose_macro(edt, dtdoctor_analyzer.dt_macro(symbol)))


def test_unknown_nodelabel_suggests_the_real_one(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, "DT_N_NODELABEL_serail0")
    assert "No node label 'serail0' exists" in out
    assert " - serial0" in out


def test_unknown_nodelabel_through_device_symbol(make_edt):
    # DEVICE_DT_GET(DT_NODELABEL(nope)) pastes '_ORD' onto the unexpanded node identifier,
    # which must not be mistaken for part of the label
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, device_symbol("DT_N_NODELABEL_serail0"))
    assert "No node label 'serail0' exists" in out
    assert " - serial0" in out


def test_api_suffixes_are_not_part_of_the_name(make_edt):
    # Whatever <devicetree.h> appended is upper case, and gen_defines.py lower-cases every
    # name it builds an identifier from, so the two never get confused
    edt, _ = make_edt(DTS_MACROS)
    for suffix in ("_ORD", "_EXISTS", "_REG_IDX_0_VAL_ADDRESS", "_P_current_speed"):
        out = diagnose(edt, f"DT_N_NODELABEL_serail0{suffix}")
        assert "No node label 'serail0' exists" in out, suffix


def test_underscores_in_a_name_are_kept(make_edt):
    # Trimming at '_' would report 'no_such' here and quietly mislead
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, device_symbol("DT_N_NODELABEL_no_such_label_at_all"))
    assert "No node label 'no_such_label_at_all' exists" in out


def test_known_nodelabel_falls_through_to_node_diagnosis(make_edt):
    # The label resolves, so what is missing is the device, not the node
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, device_symbol("DT_N_NODELABEL_serial1"))
    assert "is disabled in" in out
    assert "serial@40003000" in out


def test_unknown_alias_suggests_dts_spelling(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, device_symbol("DT_N_ALIAS_my_serail"))
    assert "No alias 'my_serail' is defined" in out
    # The alias is written with a dash in DTS but an underscore in C
    assert " - my-serial   (in C: my_serial)" in out


def test_unknown_alias_shows_the_overlay_to_write(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, "DT_N_ALIAS_totally_unrelated")
    assert "aliases {" in out
    assert "totally-unrelated = &<node label>;" in out
    # With nothing close enough to suggest, the aliases that do exist are listed instead
    assert " - my-serial" in out


def test_unknown_chosen_suggests_the_real_one(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, "DT_CHOSEN_vnd_consol")
    assert "No /chosen entry matching 'vnd_consol' is set" in out
    assert " - vnd,console   (in C: vnd_console)" in out


def test_known_chosen_falls_through_to_node_diagnosis(make_edt, kconfig_env):
    # The /chosen entry resolves to an enabled node, so what is missing is its driver
    kconfig_env("kconfig_basic")
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, device_symbol("DT_CHOSEN_vnd_console"))
    assert "no driver appears to be available" in out


def test_unknown_path_suggests_real_nodes(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, "DT_N_S_soc_S_serial_40009999")
    assert "does not name any node" in out
    assert "/soc/serial@40002000" in out


def test_unknown_path_is_not_blamed_on_an_ancestor(make_edt):
    # '/soc' is a prefix of the failing path identifier, but the node that does not exist
    # is the one the full path names
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, "DT_N_S_soc_S_nope_P_current_speed")
    assert "does not name any node" in out
    assert "'DT_N_S_soc_S_nope' does not name" in out


def test_known_path_is_matched_at_full_depth(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    edt_node_ids = dtdoctor_analyzer.build_node_id_map(edt)
    node_id, suffix = dtdoctor_analyzer.split_node_id(f"{SERIAL0_PATH_ID}_P_nope", edt_node_ids)
    assert node_id == SERIAL0_PATH_ID
    assert suffix == "_P_nope"


def test_main_end_to_end_unknown_nodelabel(make_edt, make_pickle, run_analyzer):
    edt, _ = make_edt(DTS_MACROS)
    rc, out, _ = run_analyzer(make_pickle(edt), device_symbol("DT_N_NODELABEL_serail0"))
    assert rc == 0
    assert "DT Doctor" in out
    assert "No node label 'serail0' exists" in out
