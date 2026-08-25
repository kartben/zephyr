#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Tests for dtdoctor_sca_wrapper.py error detection and analyzer dispatch."""

import subprocess
import sys

import dtdoctor_sca_wrapper
import pytest
from conftest import DTS_DISABLED_FULL, ZEPHYR_BASE, ord_symbol

WRAPPER = ZEPHYR_BASE / "scripts" / "dts" / "dtdoctor_sca_wrapper.py"


def make_fake_run(rc, stdout="", stderr=""):
    """Fake subprocess.run: the compiler call uses capture_output, analyzer calls don't."""
    calls = {"compiler": None, "analyzer": []}

    def fake_run(cmd, **kwargs):
        if kwargs.get("capture_output"):
            calls["compiler"] = cmd
            return subprocess.CompletedProcess(cmd, rc, stdout, stderr)
        calls["analyzer"].append(cmd)
        return subprocess.CompletedProcess(cmd, 0, "", "")

    return fake_run, calls


def run_wrapper(monkeypatch, argv, fake_run):
    monkeypatch.setattr(sys, "argv", ["dtdoctor_sca_wrapper.py", *argv])
    monkeypatch.setattr(subprocess, "run", fake_run)
    return dtdoctor_sca_wrapper.main()


TESTDATA_TOOLCHAIN_ERRORS = [
    (
        "main.c:10:23: error: '__device_dts_ord_7' undeclared here (not in a function); "
        "did you mean 'device_get_binding'?",
        "__device_dts_ord_7",
    ),
    (
        "main.c:12:9: error: '__device_dts_ord_7' undeclared (first use in this function)",
        "__device_dts_ord_7",
    ),
    (
        # gcc built with NLS uses Unicode quotes in UTF-8 locales
        "main.c:10:23: error: ‘__device_dts_ord_7’ undeclared here (not in a function)",
        "__device_dts_ord_7",
    ),
    (
        "main.cpp:10:11: error: '__device_dts_ord_7' was not declared in this scope",
        "__device_dts_ord_7",
    ),
    (
        "main.c:(.text+0x12): undefined reference to `__device_dts_ord_7'",
        "__device_dts_ord_7",
    ),
    (
        "main.c:10:23: error: use of undeclared identifier '__device_dts_ord_7'",
        "__device_dts_ord_7",
    ),
    (
        "ld.lld: error: undefined symbol: __device_dts_ord_7",
        "__device_dts_ord_7",
    ),
    # DEVICE_DT_GET() on a node identifier that never expanded pastes the whole macro onto
    # the device symbol, so there is no ordinal to match on
    (
        "main.c:10:23: error: '__device_dts_ord_DT_N_NODELABEL_nope_ORD' undeclared here "
        "(not in a function)",
        "__device_dts_ord_DT_N_NODELABEL_nope_ORD",
    ),
    (
        "main.c:(.text+0x12): undefined reference to `__device_dts_ord_DT_N_ALIAS_nope_ORD'",
        "__device_dts_ord_DT_N_ALIAS_nope_ORD",
    ),
    (
        "main.c:10:23: error: use of undeclared identifier "
        "'__device_dts_ord_DT_N_INST_0_vnd_nope_ORD'",
        "__device_dts_ord_DT_N_INST_0_vnd_nope_ORD",
    ),
    (
        "ld.lld: error: undefined symbol: __device_dts_ord_DT_CHOSEN_vnd_nope_ORD",
        "__device_dts_ord_DT_CHOSEN_vnd_nope_ORD",
    ),
    # Everything else reaches the compiler as a bare devicetree macro
    (
        "main.c:12:9: error: 'DT_N_S_soc_S_uart_40002000_P_nope' undeclared "
        "(first use in this function)",
        "DT_N_S_soc_S_uart_40002000_P_nope",
    ),
    (
        "main.c:10:23: error: use of undeclared identifier 'DT_N_NODELABEL_nope_P_nope'",
        "DT_N_NODELABEL_nope_P_nope",
    ),
    (
        "main.c:10:23: error: \u2018DT_CHOSEN_vnd_nope\u2019 undeclared here (not in a function)",
        "DT_CHOSEN_vnd_nope",
    ),
]


@pytest.mark.parametrize(
    'stderr_line, expected_symbol',
    TESTDATA_TOOLCHAIN_ERRORS,
    ids=[
        'gcc-file-scope',
        'gcc-function-scope',
        'gcc-utf8-quotes',
        'g++',
        'gnu-ld',
        'clang',
        'lld',
        'gcc-unexpanded-nodelabel',
        'gnu-ld-unexpanded-alias',
        'clang-unexpanded-inst',
        'lld-unexpanded-chosen',
        'gcc-bare-property-macro',
        'clang-bare-nodelabel-macro',
        'gcc-bare-chosen-macro',
    ],
)
def test_toolchain_regex_detection(monkeypatch, stderr_line, expected_symbol):
    fake_run, calls = make_fake_run(rc=1, stderr=stderr_line + "\n")
    rc = run_wrapper(
        monkeypatch, ["--edt-pickle", "edt.pickle", "--", "cc", "-c", "main.c"], fake_run
    )
    assert rc == 1
    assert calls["compiler"] == ["cc", "-c", "main.c"]
    assert len(calls["analyzer"]) == 1
    cmd = calls["analyzer"][0]
    assert cmd[0] == sys.executable
    assert cmd[1].endswith("dtdoctor_analyzer.py")
    assert cmd[2:] == ["--edt-pickle", "edt.pickle", "--symbol", expected_symbol]


def test_device_symbol_captured_whole(monkeypatch):
    # The linker pattern has to reach past '__device_dts_ord_' to the DT_N_ inside it, and
    # still report the symbol the way the linker printed it
    stderr = "main.c: undefined reference to `__device_dts_ord_DT_N_NODELABEL_nope_ORD'"
    fake_run, calls = make_fake_run(rc=1, stderr=stderr)
    run_wrapper(monkeypatch, ["--edt-pickle", "edt.pickle", "--", "cc"], fake_run)
    assert [cmd[-1] for cmd in calls["analyzer"]] == ["__device_dts_ord_DT_N_NODELABEL_nope_ORD"]


def test_unrelated_symbols_are_ignored(monkeypatch):
    stderr = "\n".join(
        [
            "main.c:10:23: error: 'CONFIG_SOMETHING' undeclared here (not in a function)",
            "main.c:11:5: error: use of undeclared identifier 'my_own_dt_helper'",
        ]
    )
    fake_run, calls = make_fake_run(rc=1, stderr=stderr)
    run_wrapper(monkeypatch, ["--edt-pickle", "edt.pickle", "--", "cc"], fake_run)
    assert calls["analyzer"] == []


def test_deduplication(monkeypatch):
    stderr = "\n".join(
        [
            "main.c:10:23: error: '__device_dts_ord_7' undeclared here (not in a function)",
            "main.c:(.text+0x12): undefined reference to `__device_dts_ord_7'",
            "main.c:11:5: error: use of undeclared identifier '__device_dts_ord_9'",
        ]
    )
    fake_run, calls = make_fake_run(rc=1, stderr=stderr)
    run_wrapper(monkeypatch, ["--edt-pickle", "edt.pickle", "--", "cc"], fake_run)
    symbols = [cmd[-1] for cmd in calls["analyzer"]]
    assert symbols == ["__device_dts_ord_7", "__device_dts_ord_9"]


def test_success_runs_no_analysis(monkeypatch):
    stderr = "main.c:10:23: error: '__device_dts_ord_7' undeclared here (not in a function)"
    fake_run, calls = make_fake_run(rc=0, stderr=stderr)
    rc = run_wrapper(monkeypatch, ["--edt-pickle", "edt.pickle", "--", "cc"], fake_run)
    assert rc == 0
    assert calls["analyzer"] == []


def test_no_edt_pickle_no_analysis(monkeypatch):
    stderr = "main.c:10:23: error: '__device_dts_ord_7' undeclared here (not in a function)"
    fake_run, calls = make_fake_run(rc=1, stderr=stderr)
    rc = run_wrapper(monkeypatch, ["--", "cc"], fake_run)
    assert rc == 1
    assert calls["analyzer"] == []


def test_compiler_output_replayed(monkeypatch, capsys):
    fake_run, _ = make_fake_run(rc=1, stdout="compiler stdout\n", stderr="compiler stderr\n")
    rc = run_wrapper(monkeypatch, ["--", "cc"], fake_run)
    out, err = capsys.readouterr()
    assert rc == 1
    assert out == "compiler stdout\n"
    assert err == "compiler stderr\n"


def test_rc_passthrough(monkeypatch):
    fake_run, _ = make_fake_run(rc=3)
    assert run_wrapper(monkeypatch, ["--edt-pickle", "edt.pickle", "--", "cc"], fake_run) == 3


def test_rc_passthrough_with_analysis(monkeypatch):
    stderr = "main.c:(.text+0x12): undefined reference to `__device_dts_ord_7'"
    fake_run, calls = make_fake_run(rc=1, stderr=stderr)
    rc = run_wrapper(monkeypatch, ["--edt-pickle", "edt.pickle", "--", "cc"], fake_run)
    assert rc == 1
    assert len(calls["analyzer"]) == 1


def test_no_double_dash_fallback(monkeypatch):
    fake_run, calls = make_fake_run(rc=0)
    run_wrapper(monkeypatch, ["cc", "-c", "main.c"], fake_run)
    assert calls["compiler"] == ["cc", "-c", "main.c"]


def test_no_double_dash_keeps_flag_in_cmd(monkeypatch):
    # Without a '--' separator the whole argv is used as the command, --edt-pickle included
    fake_run, calls = make_fake_run(rc=0)
    run_wrapper(monkeypatch, ["--edt-pickle", "edt.pickle", "cc"], fake_run)
    assert calls["compiler"] == ["--edt-pickle", "edt.pickle", "cc"]


def test_end_to_end_real_processes(make_edt, make_pickle, tmp_path):
    edt, _ = make_edt(DTS_DISABLED_FULL)
    symbol = ord_symbol(edt, "foo_dev")
    fake_cc = tmp_path / "fake_cc.py"
    fake_cc.write_text(
        "import sys\n"
        f"sys.stderr.write(\"main.c: undefined reference to `{symbol}'\\n\")\n"
        "sys.exit(1)\n",
        encoding="utf-8",
    )
    proc = subprocess.run(
        [
            sys.executable,
            str(WRAPPER),
            "--edt-pickle",
            str(make_pickle(edt)),
            "--",
            sys.executable,
            str(fake_cc),
        ],
        capture_output=True,
        text=True,
    )
    assert proc.returncode == 1
    assert "undefined reference" in proc.stderr
    assert "DT Doctor" in proc.stdout
    assert "is disabled in" in proc.stdout
