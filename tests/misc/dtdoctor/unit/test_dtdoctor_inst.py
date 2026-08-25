#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Tests for the DT_INST()/DT_DRV_COMPAT diagnosis."""

import dtdoctor_analyzer
from conftest import DTS_MACROS, DTS_MACROS_ALL_DISABLED, device_symbol


def diagnose(edt, symbol):
    return "\n".join(dtdoctor_analyzer.diagnose_macro(edt, dtdoctor_analyzer.dt_macro(symbol)))


def test_instance_index_out_of_range(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, device_symbol("DT_N_INST_2_vnd_rich_device"))
    assert "'vnd,rich-device' has 2 instance(s)" in out
    assert "so instance 2 does not exist." in out


def test_instances_listed_in_index_order(make_edt):
    # Enabled nodes come first, which is exactly why the index is easy to get wrong
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, device_symbol("DT_N_INST_2_vnd_rich_device"))
    assert ' - DT_INST(0, ...)   /soc/serial@40002000   status = "okay"' in out
    assert ' - DT_INST(1, ...)   /soc/serial@40003000   status = "disabled"' in out
    assert "DT_INST_FOREACH_STATUS_OKAY()" in out


def test_all_instances_disabled_is_called_out(make_edt):
    edt, _ = make_edt(DTS_MACROS_ALL_DISABLED)
    out = diagnose(edt, device_symbol("DT_N_INST_2_vnd_rich_device"))
    assert "None of them is enabled." in out


def test_valid_index_falls_through_even_when_all_are_disabled(make_edt):
    # DT_INST(0, ...) still resolves when every instance is disabled, and the disabled-node
    # diagnosis has more to say about it than the instance one would
    edt, _ = make_edt(DTS_MACROS_ALL_DISABLED)
    out = diagnose(edt, device_symbol("DT_N_INST_0_vnd_rich_device"))
    assert "is disabled in" in out


def test_enabled_instances_get_no_disabled_note(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, device_symbol("DT_N_INST_2_vnd_rich_device"))
    assert "None of them is enabled." not in out


def test_unknown_compatible_suggests_the_real_one(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, device_symbol("DT_N_INST_0_vnd_rich_devcie"))
    assert "No node with compatible 'vnd_rich_devcie' exists" in out
    assert " - vnd,rich-device   (in C: vnd_rich_device)" in out


def test_unknown_compatible_explains_dt_drv_compat(make_edt):
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, device_symbol("DT_N_INST_0_vnd_rich_devcie"))
    assert "#define DT_DRV_COMPAT vnd_foo_device" in out


def test_existing_instance_falls_through_to_node_diagnosis(make_edt):
    # DT_INST(1, ...) resolves, so what is missing is the device behind the node
    edt, _ = make_edt(DTS_MACROS)
    out = diagnose(edt, device_symbol("DT_N_INST_1_vnd_rich_device"))
    assert "is disabled in" in out
    assert "serial@40003000" in out


def test_malformed_instance_macro_is_not_diagnosed(make_edt):
    # Nothing sensible to say about a DT_N_INST_* macro with no instance number
    edt, _ = make_edt(DTS_MACROS)
    assert dtdoctor_analyzer.diagnose_macro(edt, "DT_N_INST_nonsense") == []


def test_main_end_to_end_unknown_instance(make_edt, make_pickle, run_analyzer):
    edt, _ = make_edt(DTS_MACROS)
    rc, out, _ = run_analyzer(make_pickle(edt), device_symbol("DT_N_INST_2_vnd_rich_device"))
    assert rc == 0
    assert "DT Doctor" in out
    assert "so instance 2 does not exist." in out
