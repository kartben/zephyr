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


@pytest.mark.parametrize("symbol", ["some_random_symbol", "DT_FOO", "z_impl_k_sleep"])
def test_unrecognized_symbol(env, symbol):
    assert dtdoc.diagnose(env.edt, symbol) is None


def test_wrapper_extract_symbols_gcc():
    stderr = (
        "main.c:10:5: error: '__device_dts_ord_123' undeclared here (not in a function); "
        "did you mean '__device_dts_ord_16'?\n"
    )
    assert wrapper.extract_symbols(stderr) == {"__device_dts_ord_123"}


def test_wrapper_extract_symbols_clang():
    stderr = "main.c:10:5: error: use of undeclared identifier '__device_dts_ord_42'\n"
    assert wrapper.extract_symbols(stderr) == {"__device_dts_ord_42"}


def test_wrapper_extract_symbols_ld():
    stderr = "main.c:(.text+0x4): undefined reference to `__device_dts_ord_7'\n"
    assert wrapper.extract_symbols(stderr) == {"__device_dts_ord_7"}


def test_wrapper_extract_symbols_lld():
    stderr = "ld.lld: error: undefined symbol: __device_dts_ord_11\n"
    assert wrapper.extract_symbols(stderr) == {"__device_dts_ord_11"}


def test_wrapper_extract_symbols_no_match():
    stderr = "main.c:10:5: error: 'foo' undeclared (first use in this function)\n"
    assert wrapper.extract_symbols(stderr) == set()
