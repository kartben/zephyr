#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors

"""
Tests for dtdoctor_analyzer.py and dtdoctor_sca_wrapper.py.

The diagnosis tests compare the full analyzer output against the expected text,
with the temporary fixture directory normalized to '<test-data>'.

Run with:

    pytest ./scripts/dts/dtdoctor_test.py
"""

import pickle
import sys
import textwrap
from pathlib import Path
from types import SimpleNamespace

import pytest

sys.path.insert(0, str(Path(__file__).parent))

import dtdoctor_analyzer as dtdoc
import dtdoctor_sca_wrapper as wrapper
import kconfiglib
from devicetree import edtlib

TEST_DTS = """
/dts-v1/;

/ {
    #address-cells = <1>;
    #size-cells = <1>;

    aliases {
        my-uart = &uart0;
        backup-uart = &uart1;
    };

    chosen {
        zephyr,console = &uart0;
        zephyr,shell-uart = &uart1;
    };

    soc {
        #address-cells = <1>;
        #size-cells = <1>;

        uart0: uart@1000 {
            compatible = "vnd,serial";
            reg = <0x1000 0x100>;
            status = "okay";
            current-speed = <115200>;
            calibration = <1 2 3>;
        };

        uart1: uart@2000 {
            compatible = "vnd,serial";
            reg = <0x2000 0x100>;
            status = "disabled";
        };

        i2c0: i2c@3000 {
            compatible = "vnd,i2c";
            reg = <0x3000 0x100>;
            status = "okay";
            #address-cells = <1>;
            #size-cells = <0>;

            sensor0: sensor@10 {
                compatible = "vnd,sensor";
                reg = <0x10>;
                status = "okay";
            };
        };
    };
};
"""

SERIAL_BINDING = """
description: Test UART

compatible: "vnd,serial"

properties:
  reg:
    type: array
    required: true
  current-speed:
    type: int
  fifo-depth:
    type: int
    description: Depth of the hardware FIFO
  calibration:
    type: array
"""

I2C_BINDING = """
description: Test I2C controller

compatible: "vnd,i2c"

properties:
  reg:
    type: array
    required: true
"""

SENSOR_BINDING = """
description: Test sensor

compatible: "vnd,sensor"

properties:
  reg:
    type: array
    required: true
"""

TEST_KCONFIG = """
config DT_HAS_VND_SERIAL_ENABLED
	bool
	default y

config SERIAL
	bool "serial driver support"

config UART_VND
	bool "vnd uart driver"
	default y
	depends on SERIAL && DT_HAS_VND_SERIAL_ENABLED
"""


@pytest.fixture(scope="module")
def env(tmp_path_factory) -> SimpleNamespace:
    tmp_path = tmp_path_factory.mktemp("dtdoctor")

    dts = tmp_path / "test.dts"
    dts.write_text(TEST_DTS)

    bindings = tmp_path / "bindings"
    bindings.mkdir()
    (bindings / "vnd,serial.yaml").write_text(SERIAL_BINDING)
    (bindings / "vnd,i2c.yaml").write_text(I2C_BINDING)
    (bindings / "vnd,sensor.yaml").write_text(SENSOR_BINDING)

    kconfig = tmp_path / "Kconfig"
    kconfig.write_text(TEST_KCONFIG)

    return SimpleNamespace(
        edt=edtlib.EDT(str(dts), [str(bindings)]),
        kconf=kconfiglib.Kconfig(str(kconfig), warn=False),
        path=tmp_path,
    )


@pytest.fixture
def check(env):
    def _check(symbol: str, expected: str, kconf_factory=None):
        lines = dtdoc.diagnose(env.edt, symbol, kconf_factory=kconf_factory)
        assert lines is not None, f"symbol '{symbol}' was not recognized"

        out = "\n".join(lines)
        out = out.replace(str(env.path), "<test-data>")
        assert out == textwrap.dedent(expected).strip("\n")

    return _check


def test_disabled_node(env, check):
    ordinal = env.edt.label2node["uart1"].dep_ordinal
    # The diagnosis points at the line disabling the node in the fixture DTS
    lineno = next(i for i, line in enumerate(TEST_DTS.splitlines(), 1) if "disabled" in line)
    check(
        f"__device_dts_ord_{ordinal}",
        f"""
        'uart1: /soc/uart@2000' is disabled in <test-data>/test.dts:{lineno}

        It is referenced as a "chosen" in 'zephyr,shell-uart'
        It is referenced by the following aliases: 'backup-uart'

        Try enabling the node by setting its 'status' property to 'okay'.
        """,
    )


def test_enabled_node_no_driver(env, check):
    # Without a Kconfig tree, only generic guidance mentioning the compatible is given
    ordinal = env.edt.label2node["uart0"].dep_ordinal
    check(
        f"__device_dts_ord_{ordinal}",
        """
        'uart0: /soc/uart@1000' is enabled but no driver appears to be available for it.

        Check that a driver for compatible 'vnd,serial' exists and that
        the Kconfig options gating it are enabled.
        """,
    )


def test_enabled_node_kconfig_suggestions(env, check):
    ordinal = env.edt.label2node["uart0"].dep_ordinal
    check(
        f"__device_dts_ord_{ordinal}",
        """
        'uart0: /soc/uart@1000' is enabled but no driver appears to be available for it.

        Try enabling these Kconfig options:

         - CONFIG_SERIAL=y
        """,
        kconf_factory=lambda: env.kconf,
    )


def test_find_kconfig_deps(env):
    deps = dtdoc.find_kconfig_deps(env.kconf, "DT_HAS_VND_SERIAL_ENABLED")
    assert deps == {"CONFIG_SERIAL"}


def test_stale_ordinal(check):
    check(
        "__device_dts_ord_9999",
        """
        No devicetree node with dependency ordinal 9999 was found.

        The build directory may be out of date; try a pristine build.
        """,
    )


def test_missing_alias(check):
    check(
        "DT_N_ALIAS_my_usrt_P_gpios_IDX_0_PH_ORD",
        """
        Devicetree alias 'my_usrt' does not exist.

        Note: in this identifier, characters like '-' in the actual alias name
        appear as '_'.

        Did you mean one of these existing aliases?
         - my-uart (uart0: /soc/uart@1000)

        To define the alias, add something like this to your devicetree overlay:

            / {
                    aliases {
                            my-usrt = &some_node_label;
                    };
            };
        """,
    )


def test_missing_nodelabel(check):
    check(
        "DT_N_NODELABEL_usrt0_ORD",
        """
        No devicetree node has the node label 'usrt0'.

        Note: DT_NODELABEL() expects the lowercase-and-underscores form of the label.

        Did you mean one of these existing node labels?
         - uart0 (/soc/uart@1000)
         - uart1 (/soc/uart@2000)
        """,
    )


def test_missing_chosen(check):
    check(
        "DT_CHOSEN_zephyr_consol",
        """
        The devicetree has no chosen property named 'zephyr_consol'.

        Note: in this identifier, characters like ',' or '-' in the actual chosen
        property name appear as '_'.

        Did you mean one of these existing chosen properties?
         - zephyr,console (uart0: /soc/uart@1000)
         - zephyr,shell-uart (uart1: /soc/uart@2000)

        To define it, add something like this to your devicetree overlay:

            / {
                    chosen {
                            some,property = &some_node_label;
                    };
            };
        """,
    )


def test_missing_node_path(check):
    check(
        "DT_N_S_soc_S_spi_4000_P_reg",
        """
        No node with path '/soc/spi_4000' exists in the final devicetree.

        Note: in this path, characters like '@', '-' or ',' appear as '_'.

        The closest existing ancestor is '/soc', whose children are:
         - uart@1000
         - uart@2000
         - i2c@3000

        Did you mean one of these existing nodes?
         - /soc/i2c@3000
         - /soc/uart@2000
         - /soc/uart@1000
        """,
    )


def test_missing_child_node_path(check):
    check(
        "DT_N_S_soc_S_i2c_3000_S_sensor_20_P_reg",
        """
        No node with path '/soc/i2c@3000/sensor_20' exists in the final devicetree.

        Note: in this path, characters like '@', '-' or ',' appear as '_'.

        The closest existing ancestor is 'i2c0: /soc/i2c@3000', whose children are:
         - sensor@10

        Did you mean one of these existing nodes?
         - /soc/i2c@3000/sensor@10
         - /soc/i2c@3000
         - /soc/uart@2000
        """,
    )


def test_missing_property_declared_in_binding(check):
    check(
        "DT_N_S_soc_S_uart_1000_P_fifo_depth",
        """
        Node 'uart0: /soc/uart@1000' does not set the 'fifo-depth' property.

        Description: Depth of the hardware FIFO
        Type: int
        Binding: <test-data>/bindings/vnd,serial.yaml

        To set it, add something like this to your devicetree overlay:

            &uart0 {
                    fifo-depth = <...>;
            };
        """,
    )


def test_property_typo(check):
    check(
        "DT_N_S_soc_S_uart_1000_P_curent_speed",
        """
        Node 'uart0: /soc/uart@1000' has no property 'curent_speed'.

        Did you mean one of these properties of the node?
         - current-speed

        Note: devicetree macros are only generated for properties that are
        declared in the node's binding.
        Binding: <test-data>/bindings/vnd,serial.yaml
        """,
    )


def test_property_index_out_of_range(check):
    check(
        "DT_N_S_soc_S_uart_1000_P_calibration_IDX_5",
        """
        Index 5 of property 'calibration' on node 'uart0: /soc/uart@1000' is out of range.

        The property only has 3 element(s) (valid indexes: 0 to 2).
        """,
    )


def test_property_set_but_wrong_accessor(check):
    check(
        "DT_N_S_soc_S_uart_1000_P_current_speed_STRING_TOKEN",
        """
        Property 'current-speed' on node 'uart0: /soc/uart@1000' is set, but the
        accessed macro does not exist.

        Check that the devicetree API used matches the property's type ('int').
        """,
    )


def test_instance_out_of_range(check):
    check(
        "DT_N_INST_3_vnd_serial_P_current_speed",
        """
        There is no instance 3 of compatible 'vnd,serial'.

        There are 1 enabled node(s) with this compatible (valid instance numbers: 0 to 0):
         - uart0: /soc/uart@1000

        These nodes have the right compatible but are not enabled:
         - uart1: /soc/uart@2000 (status: disabled)

        Try setting their 'status' property to 'okay'.
        """,
    )


def test_instance_unknown_compat(check):
    check(
        "DT_N_INST_0_vnd_serail_P_current_speed",
        """
        No devicetree node has a compatible matching 'vnd_serail'.

        Did you mean one of these compatibles?
         - vnd,serial
         - vnd,sensor
        """,
    )


def test_device_get_on_missing_nodelabel(check):
    # DEVICE_DT_GET(DT_NODELABEL(usrt0)) leaves an unresolved __device_dts_ord_DT_..._ORD
    # symbol; the inner node identifier is diagnosed
    check(
        "__device_dts_ord_DT_N_NODELABEL_usrt0_ORD",
        """
        No devicetree node has the node label 'usrt0'.

        Note: DT_NODELABEL() expects the lowercase-and-underscores form of the label.

        Did you mean one of these existing node labels?
         - uart0 (/soc/uart@1000)
         - uart1 (/soc/uart@2000)
        """,
    )


@pytest.mark.parametrize("symbol", ["some_random_symbol", "DT_FOO", "z_impl_k_sleep"])
def test_unrecognized_symbol(env, symbol):
    assert dtdoc.diagnose(env.edt, symbol) is None


def test_wrapper_extract_symbols_gcc():
    stderr = (
        "main.c:10:5: error: '__device_dts_ord_123' undeclared here (not in a function); "
        "did you mean '__device_dts_ord_16'?\n"
        "main.c:12:5: error: 'DT_N_NODELABEL_foo_ORD' undeclared (first use in this function)\n"
    )
    assert wrapper.extract_symbols(stderr) == {
        "__device_dts_ord_123",
        "DT_N_NODELABEL_foo_ORD",
    }


def test_wrapper_extract_symbols_clang():
    stderr = (
        "main.c:10:5: error: use of undeclared identifier '__device_dts_ord_42'\n"
        "main.c:12:5: error: use of undeclared identifier 'DT_N_ALIAS_led0_P_gpios_IDX_0_PH_ORD'\n"
        "main.c:14:5: error: use of undeclared identifier 'DT_CHOSEN_zephyr_display'\n"
    )
    assert wrapper.extract_symbols(stderr) == {
        "__device_dts_ord_42",
        "DT_N_ALIAS_led0_P_gpios_IDX_0_PH_ORD",
        "DT_CHOSEN_zephyr_display",
    }


def test_wrapper_extract_symbols_ld():
    stderr = "main.c:(.text+0x4): undefined reference to `__device_dts_ord_7'\n"
    assert wrapper.extract_symbols(stderr) == {"__device_dts_ord_7"}


def test_wrapper_extract_symbols_lld():
    stderr = "ld.lld: error: undefined symbol: __device_dts_ord_11\n"
    assert wrapper.extract_symbols(stderr) == {"__device_dts_ord_11"}


def test_wrapper_extract_symbols_no_match():
    stderr = "main.c:10:5: error: 'foo' undeclared (first use in this function)\n"
    assert wrapper.extract_symbols(stderr) == set()


def test_wrapper_extract_symbols_colored():
    # Zephyr builds pass -fdiagnostics-color=always, so the captured stderr contains
    # ANSI escape sequences, including inside the quoted identifiers
    stderr = (
        "main.c:23:15: \x1b[01;31m\x1b[Kerror: \x1b[m\x1b[K"
        "'\x1b[01m\x1b[K__device_dts_ord_DT_CHOSEN_zephyr_display_ORD\x1b[m\x1b[K'"
        " undeclared (first use in this function)\n"
    )
    assert wrapper.extract_symbols(stderr) == {"__device_dts_ord_DT_CHOSEN_zephyr_display_ORD"}


def test_wrapper_extract_symbols_unicode_quotes():
    # gcc quotes identifiers with Unicode quotation marks in UTF-8 locales
    stderr = "main.c:10:5: error: ‘DT_N_NODELABEL_foo_ORD’ undeclared\n"
    assert wrapper.extract_symbols(stderr) == {"DT_N_NODELABEL_foo_ORD"}


def test_wrapper_dedupes_diagnoses(env, tmp_path):
    # A single faulty source line typically produces several distinct symbols that
    # reduce to the same root cause: only one diagnosis should be reported
    edt_pickle = tmp_path / "edt.pickle"
    edt_pickle.write_bytes(pickle.dumps(env.edt))

    outputs = wrapper.run_diagnostics(
        {
            "__device_dts_ord_DT_N_ALIAS_my_usrt_P_gpios_IDX_0_PH_ORD",
            "DT_N_ALIAS_my_usrt_P_gpios_IDX_0_VAL_pin",
        },
        str(edt_pickle),
    )

    assert len(outputs) == 1
    assert "alias 'my_usrt' does not exist" in outputs[0]
