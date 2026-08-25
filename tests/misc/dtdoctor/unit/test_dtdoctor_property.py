#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Tests for the diagnosis of property macros a node does not have."""

import dtdoctor_analyzer
from conftest import DTS_MACROS, SERIAL0_PATH_ID


def diagnose(edt, macro):
    return "\n".join(dtdoctor_analyzer.diagnose_macro(edt, macro))


def prop_macro(prop_id, node_id=SERIAL0_PATH_ID):
    return f"{node_id}_P_{prop_id}"


def test_unknown_property_names_the_node(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, prop_macro("nothing_like_this"))
    assert "'serial0: /soc/serial@40002000' has no 'nothing_like_this' property." in out


def test_unknown_property_lists_the_real_ones(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, prop_macro("nothing_like_this"))
    assert "Properties this node does have:" in out
    assert " - current-speed   (in C: current_speed)" in out


def test_typo_suggests_the_real_property(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, prop_macro("currentspeed"))
    assert "Did you mean one of these?" in out
    assert " - current-speed   (in C: current_speed)" in out


def test_dash_versus_underscore_is_spelled_out(make_edt):
    # 'pin-names' in DTS is pin_names in C, which is the mistake this hint exists for
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, prop_macro("pin_nmaes"))
    assert " - pin-names   (in C: pin_names)" in out
    assert "In C, property names are lowercased" in out


def test_declared_by_binding_but_unset(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, prop_macro("optional_thing"))
    assert "binding declares 'optional-thing', but the node does not set it" in out
    assert "DT_PROP_OR()" in out
    assert "DT_NODE_HAS_PROP()" in out


def test_binding_path_reported(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, prop_macro("nothing_like_this"))
    assert "vnd,rich-device.yaml" in out


def test_property_exists_but_suffix_does_not(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, prop_macro("current_speed_IDX_3"))
    assert "has a 'current-speed' property, but 'IDX_3' was" in out
    assert "'current-speed' is of type 'int'." in out


def test_property_element_count_reported(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, prop_macro("pin_names_IDX_9"))
    assert "'pin-names' is of type 'string-array', with 2 element(s)." in out


def test_property_without_a_plain_value_macro(make_edt):
    # A phandle has no single C rvalue, so gen_defines.py emits no DT_..._P_friend macro
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, prop_macro("friend"))
    assert "no macro holding a" in out
    assert "'friend' is of type 'phandle'" in out
    assert "DT_PHANDLE_BY_IDX()" in out


def test_property_reached_through_a_nodelabel(make_edt):
    # The node identifier can be any spelling; only what follows '_P_' is the property
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, prop_macro("currentspeed", node_id="DT_N_NODELABEL_serial0"))
    assert "has no 'currentspeed' property" in out
    assert " - current-speed   (in C: current_speed)" in out


def test_main_end_to_end_unknown_property(make_edt, make_pickle, run_analyzer):
    edt, _ = make_edt(DTS_MACROS)
    rc, out, _ = run_analyzer(make_pickle(edt), prop_macro("currentspeed"))
    assert rc == 0
    assert "DT Doctor" in out
    assert "has no 'currentspeed' property" in out
