#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Tests for the specifier diagnoses: phandle-array cells, interrupts and reg."""

import dtdoctor_analyzer
import pytest
from conftest import CONSUMER_PATH_ID, DTS_SPECIFIERS


def diagnose(edt, suffix, node_id=CONSUMER_PATH_ID):
    return "\n".join(dtdoctor_analyzer.diagnose_macro(edt, f"{node_id}{suffix}"))


TESTDATA_SPECIFIER_SHAPES = [
    ("_IDX_0_VAL_pin", {"idx": "0", "name": None, "cell": "pin", "extra": None}),
    ("_IDX_0_VAL_pin_EXISTS", {"idx": "0", "name": None, "cell": "pin", "extra": "EXISTS"}),
    ("_NAME_red_VAL_pin", {"idx": None, "name": "red", "cell": "pin", "extra": None}),
    ("_IDX_1_PH", {"idx": "1", "name": None, "cell": None, "extra": "PH"}),
    ("_IDX_1_NUM_CELLS", {"idx": "1", "name": None, "cell": None, "extra": "NUM_CELLS"}),
    ("_IDX_1", {"idx": "1", "name": None, "cell": None, "extra": None}),
]


@pytest.mark.parametrize('suffix, expected', TESTDATA_SPECIFIER_SHAPES)
def test_specifier_shapes_parsed(suffix, expected):
    # The parser is the part most exposed to <devicetree.h> growing new accessors, and the
    # upper/lower case split is the only thing keeping '_VAL_' out of a name
    m = dtdoctor_analyzer.SPECIFIER_RE.match(suffix)
    assert m is not None
    assert m.groupdict() == expected


def test_non_specifier_suffix_not_parsed():
    assert dtdoctor_analyzer.SPECIFIER_RE.match("_LEN") is None
    assert dtdoctor_analyzer.SPECIFIER_RE.match("_FOREACH_PROP_ELEM") is None


def test_unknown_gpio_cell_names_the_controller(make_edt):
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_P_gpios_IDX_0_VAL_pen")
    assert "has no 'pen' cell in entry 0 of the 'gpios' property." in out
    assert "Entry 0 is controlled by 'gpio0: /gpio@40001000'" in out


def test_unknown_gpio_cell_lists_cells_and_values(make_edt):
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_P_gpios_IDX_1_VAL_pen")
    assert " - pin     (currently 14)" in out
    assert " - flags   (currently 0)" in out


def test_unknown_gpio_cell_suggests_and_points_at_the_binding(make_edt):
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_P_gpios_IDX_0_VAL_pen")
    assert " - pin" in out
    # The cells are declared by the controller's binding, not the consumer's
    assert "not this node's" in out
    assert "vnd,gpio-ctrl.yaml" in out


def test_gpio_index_out_of_range(make_edt):
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_P_gpios_IDX_5_VAL_pin")
    assert "has no entry 5 in the 'gpios' property: there are only 2." in out
    assert " - index 0   gpio0: /gpio@40001000   named 'red'" in out
    assert " - index 1   gpio0: /gpio@40001000   named 'green'" in out


def test_gpio_name_not_found(make_edt):
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_P_gpios_NAME_blue_VAL_pin")
    assert "has no entry named 'blue' in the 'gpios' property." in out
    # Names are keyed by the specifier space, so 'gpio-names' rather than 'gpios-names'
    assert "Entry names come from its 'gpio-names' property:" in out
    assert " - red" in out


def test_cells_are_not_gpio_specific(make_edt):
    # Nothing here keys off 'gpios': the handler runs on any phandle-array property, of
    # which Zephyr's in-tree bindings declare several hundred distinct names
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_P_pwms_IDX_0_VAL_chanel")
    assert "has no 'chanel' cell in entry 0 of the 'pwms' property." in out
    assert "Entry 0 is controlled by 'pwm0: /pwm@40003000'" in out
    assert " - channel   (currently 3)" in out
    assert " - period    (currently 5000)" in out


def test_non_gpio_specifier_index_out_of_range(make_edt):
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_P_pwms_IDX_2_VAL_channel")
    assert "has no entry 2 in the 'pwms' property: there is only 1." in out


def test_unknown_interrupt_cell(make_edt):
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_IRQ_IDX_0_VAL_priorty")
    assert "has no 'priorty' cell in entry 0 of its interrupts." in out
    assert "Entry 0 is controlled by 'irq0: /interrupt-controller@e000e100'" in out
    assert " - irq        (currently 5)" in out
    assert " - priority" in out


def test_interrupt_index_out_of_range(make_edt):
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_IRQ_IDX_3_VAL_irq")
    assert "has no entry 3 in its interrupts: there is only 1." in out


def test_interrupt_name_lookup_without_names(make_edt):
    # The fixture has no 'interrupt-names', which is a different problem from a name typo
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_IRQ_NAME_tx_VAL_irq")
    assert "does not name the entries of its interrupts, so 'tx' cannot be" in out
    assert "come from an 'interrupt-names' property" in out


def test_reg_index_out_of_range_shows_addresses(make_edt):
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_REG_IDX_2_VAL_ADDRESS")
    assert "has no entry 2 in its registers: there is only 1." in out
    assert " - index 0   at 0x40002000" in out


def test_reg_on_a_node_without_one(make_edt):
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_REG_IDX_0_VAL_ADDRESS", node_id="DT_N_S_gpio_40001000")
    assert "has no 'reg' property" in out
    assert "DT_NODE_HAS_PROP()" in out


def test_valid_entry_without_a_cell_falls_through(make_edt):
    # Entry 0 is there and no cell was named, so the specifier handler has nothing to say
    # and the generic property message has to stay
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_P_gpios_IDX_0_PH")
    assert "but 'IDX_0_PH' was\nnot generated for it." in out
    assert "'gpios' is of type 'phandle-array'" in out


def test_non_specifier_property_suffix_unaffected(make_edt):
    edt, _ = make_edt(DTS_SPECIFIERS)
    out = diagnose(edt, "_P_gpios_LEN")
    assert "but 'LEN' was\nnot generated for it." in out


def test_main_end_to_end_unknown_cell(make_edt, make_pickle, run_analyzer):
    edt, _ = make_edt(DTS_SPECIFIERS)
    rc, out, _ = run_analyzer(make_pickle(edt), f"{CONSUMER_PATH_ID}_P_gpios_IDX_0_VAL_pen")
    assert rc == 0
    assert "DT Doctor" in out
    assert "has no 'pen' cell in entry 0" in out
