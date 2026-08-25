# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""
End-to-end DT Doctor checks, run by ctest against a real application build.

The analyzer and the SCA wrapper are exercised as real subprocesses against the
build's edt.pickle. The deliberately-failing translation units use the real
devicetree macros and are compiled with the application's own compile commands
(replayed from compile_commands.json), so the whole chain is real:
gen_defines.py output, <devicetree.h> expansion, toolchain message, wrapper,
analyzer.
"""

import subprocess
import sys
from pathlib import Path

import pytest
from conftest import retarget

ZEPHYR_BASE = Path(__file__).parents[4]
ANALYZER = ZEPHYR_BASE / "scripts" / "dts" / "dtdoctor_analyzer.py"
WRAPPER = ZEPHYR_BASE / "scripts" / "dts" / "dtdoctor_sca_wrapper.py"

HEADER = "#include <zephyr/device.h>\n#include <zephyr/devicetree.h>\n\n"

# DEVICE_DT_GET() on the disabled node fails at compile time: <zephyr/device.h>
# only declares device symbols for status "okay" nodes
BAD_COMPILE_SNIPPETS = [
    "const struct device *bad = DEVICE_DT_GET(DT_NODELABEL(dtdoctor_disabled));\n",
    "const struct device *get_bad(void)\n"
    "{\n"
    "\treturn DEVICE_DT_GET(DT_NODELABEL(dtdoctor_disabled));\n"
    "}\n",
]

# DEVICE_DT_GET() on the enabled, driver-less node compiles (the symbol is
# declared) and fails at link time instead
BAD_LINK_SNIPPET = (
    "const struct device *okdev = DEVICE_DT_GET(DT_NODELABEL(dtdoctor_enabled));\n"
    "int main(void)\n"
    "{\n"
    "\treturn okdev != (const struct device *)0;\n"
    "}\n"
)


def ord_symbol(edt, label):
    return f"__device_dts_ord_{edt.label2node[label].dep_ordinal}"


def device_symbol(node_id):
    """
    The symbol DEVICE_DT_GET() pastes together for a node identifier that never expanded,
    i.e. what the build error shows for DEVICE_DT_GET(DT_NODELABEL(nope)).
    """
    return f"__device_dts_ord_{node_id}_ORD"


def run_analyzer(edt_pickle, symbol):
    return subprocess.run(
        [sys.executable, str(ANALYZER), "--edt-pickle", str(edt_pickle), "--symbol", symbol],
        capture_output=True,
        text=True,
    )


def run_wrapper_around(cmd, edt_pickle, cwd=None):
    return subprocess.run(
        [sys.executable, str(WRAPPER), "--edt-pickle", str(edt_pickle), "--", *cmd],
        capture_output=True,
        text=True,
        cwd=cwd,
    )


def test_analyzer_reports_disabled_node(edt, edt_pickle):
    proc = run_analyzer(edt_pickle, ord_symbol(edt, "dtdoctor_disabled"))
    assert proc.returncode == 0
    assert "DT Doctor" in proc.stdout
    assert "is disabled in" in proc.stdout
    assert "dtdoctor-disabled-device" in proc.stdout
    assert "'dtdoctor,dev'" in proc.stdout
    assert "'dtdoctor-dev'" in proc.stdout
    assert "'status' property to 'okay'" in proc.stdout


def test_analyzer_reports_enabled_node_without_driver(edt, edt_pickle):
    proc = run_analyzer(edt_pickle, ord_symbol(edt, "dtdoctor_enabled"))
    assert proc.returncode == 0
    assert "is enabled but no driver" in proc.stdout


def test_analyzer_reports_disabled_parent(edt, edt_pickle):
    proc = run_analyzer(edt_pickle, ord_symbol(edt, "dtdoctor_nested"))
    assert proc.returncode == 0
    assert "is enabled but no driver" in proc.stdout
    assert "Its parent 'dtdoctor_bus: /dtdoctor-disabled-bus' is disabled in" in proc.stdout


# Every macro shape below is one the compiler emits verbatim when its node identifier does
# not resolve, checked here against the build's real devicetree rather than a fixture.
TESTDATA_MACROS = [
    (
        device_symbol("DT_N_NODELABEL_dtdoctor_no_such_label"),
        "No node label 'dtdoctor_no_such_label' exists",
    ),
    (
        device_symbol("DT_N_ALIAS_dtdoctor_no_such_alias"),
        "No alias 'dtdoctor_no_such_alias' is defined",
    ),
    (
        device_symbol("DT_CHOSEN_dtdoctor_no_such_chosen"),
        "No /chosen entry matching 'dtdoctor_no_such_chosen' is set",
    ),
    (
        "DT_N_NODELABEL_dtdoctor_enabled_P_no_such_prop",
        "has no 'no_such_prop' property",
    ),
    (
        "DT_N_NODELABEL_dtdoctor_enabled_P_dtdoctor_optional",
        "binding declares 'dtdoctor-optional', but the node does not set it",
    ),
    (
        device_symbol("DT_N_INST_5_vnd_dtdoctor_device"),
        "so instance 5 does not exist",
    ),
    (
        device_symbol("DT_N_INST_0_vnd_no_such_compat"),
        "No node with compatible 'vnd_no_such_compat' exists",
    ),
    (
        "DT_N_NODELABEL_dtdoctor_enabled_P_dtdoctor_gpios_IDX_0_VAL_pn",
        "has no 'pn' cell in entry 0 of the 'dtdoctor-gpios' property",
    ),
    (
        "DT_N_NODELABEL_dtdoctor_enabled_P_dtdoctor_gpios_IDX_4_VAL_pin",
        "has no entry 4 in the 'dtdoctor-gpios' property: there is only 1",
    ),
    (
        "DT_N_NODELABEL_dtdoctor_enabled_P_dtdoctor_gpios_NAME_blue_VAL_pin",
        "has no entry named 'blue' in the 'dtdoctor-gpios' property",
    ),
]


@pytest.mark.parametrize(
    'symbol, expected',
    TESTDATA_MACROS,
    ids=[
        'nodelabel',
        'alias',
        'chosen',
        'property',
        'unset-property',
        'instance',
        'compatible',
        'cell',
        'cell-index',
        'cell-name',
    ],
)
def test_analyzer_reverse_engineers_macro(edt_pickle, symbol, expected):
    proc = run_analyzer(edt_pickle, symbol)
    assert proc.returncode == 0
    assert "DT Doctor" in proc.stdout
    assert expected in proc.stdout


def test_analyzer_names_the_cell_controller(edt_pickle):
    # The cells come from the controller's binding, not from the node the macro names,
    # which is the indirection the diagnosis exists to spare the user
    proc = run_analyzer(edt_pickle, "DT_N_NODELABEL_dtdoctor_enabled_P_dtdoctor_gpios_IDX_0_VAL_pn")
    assert proc.returncode == 0
    assert "dtdoctor_gpio: /dtdoctor-gpio-controller" in proc.stdout
    assert " - pin" in proc.stdout
    assert "vnd,dtdoctor-gpio.yaml" in proc.stdout


def test_analyzer_uses_the_specifier_space_for_names(edt_pickle):
    # 'dtdoctor-gpios' entries are named through 'gpio-names', not 'dtdoctor-gpio-names'
    proc = run_analyzer(
        edt_pickle, "DT_N_NODELABEL_dtdoctor_enabled_P_dtdoctor_gpios_NAME_blue_VAL_pin"
    )
    assert proc.returncode == 0
    assert "Entry names come from its 'gpio-names' property:" in proc.stdout
    assert " - red" in proc.stdout


def test_analyzer_suggests_a_real_nodelabel(edt_pickle):
    # A near miss on a label that does exist has to come back as a suggestion
    proc = run_analyzer(edt_pickle, device_symbol("DT_N_NODELABEL_dtdoctor_enabld"))
    assert proc.returncode == 0
    assert " - dtdoctor_enabled" in proc.stdout


def test_analyzer_resolves_a_valid_nodelabel(edt_pickle):
    # The label resolves, so the diagnosis is about the node, not the identifier
    proc = run_analyzer(edt_pickle, device_symbol("DT_N_NODELABEL_dtdoctor_disabled"))
    assert proc.returncode == 0
    assert "is disabled in" in proc.stdout


@pytest.mark.parametrize('suffix', ['src/main.c', 'src/main.cpp'], ids=['c', 'cpp'])
@pytest.mark.parametrize('snippet', BAD_COMPILE_SNIPPETS, ids=['file-scope', 'function-scope'])
def test_wrapper_diagnoses_compile_error(compile_cmd, edt_pickle, tmp_path, suffix, snippet):
    argv, cwd, source = compile_cmd(suffix)
    bad_src = tmp_path / f"bad{Path(suffix).suffix}"
    bad_src.write_text(HEADER + snippet, encoding="utf-8")

    cmd = retarget(argv, source, bad_src, tmp_path / "bad.obj")
    proc = run_wrapper_around(cmd, edt_pickle, cwd=cwd)
    assert proc.returncode != 0

    # Pin the real toolchain spelling the wrapper regexes exist for: g++ has a
    # C++-only one, while clang++ shares the C message
    if suffix.endswith(".cpp") and "g++" in Path(argv[0]).name:
        assert "was not declared" in proc.stderr
    else:
        assert "undeclared" in proc.stderr

    assert "DT Doctor" in proc.stdout
    assert "is disabled in" in proc.stdout


# Real macro uses whose node identifier never resolves, so the unexpanded macro
# leaks into the compiler output — the wrapper has to recognise it in the real
# toolchain's own error format
TESTDATA_UNEXPANDED = [
    (
        "const struct device *bad = DEVICE_DT_GET(DT_NODELABEL(dtdoctor_no_such_label));\n",
        "No node label 'dtdoctor_no_such_label' exists",
    ),
    (
        "int bad = DT_PROP(DT_NODELABEL(dtdoctor_enabled), no_such_prop);\n",
        "has no 'no_such_prop' property",
    ),
    (
        "const struct device *bad = DEVICE_DT_GET(DT_INST(5, vnd_dtdoctor_device));\n",
        "so instance 5 does not exist",
    ),
    (
        "int bad = DT_PHA_BY_IDX(DT_NODELABEL(dtdoctor_enabled), dtdoctor_gpios, 0, pn);\n",
        "has no 'pn' cell in entry 0",
    ),
]


@pytest.mark.parametrize(
    'snippet, expected',
    TESTDATA_UNEXPANDED,
    ids=['nodelabel', 'property', 'instance', 'cell'],
)
def test_wrapper_diagnoses_unexpanded_macro(compile_cmd, edt_pickle, tmp_path, snippet, expected):
    argv, cwd, source = compile_cmd('src/main.c')
    bad_src = tmp_path / "bad.c"
    bad_src.write_text(HEADER + snippet, encoding="utf-8")

    cmd = retarget(argv, source, bad_src, tmp_path / "bad.obj")
    proc = run_wrapper_around(cmd, edt_pickle, cwd=cwd)
    assert proc.returncode != 0
    assert "DT Doctor" in proc.stdout
    assert expected in proc.stdout


def test_wrapper_diagnoses_link_error(compile_cmd, edt_pickle, cc, tmp_path):
    argv, cwd, source = compile_cmd('src/main.c')
    bad_src = tmp_path / "bad.c"
    bad_src.write_text(HEADER + BAD_LINK_SNIPPET, encoding="utf-8")

    obj = tmp_path / "bad.obj"
    subprocess.run(retarget(argv, source, bad_src, obj), cwd=cwd, check=True)

    link_cmd = [cc, str(obj), "-nostdlib", "-o", str(tmp_path / "bad.elf")]
    proc = run_wrapper_around(link_cmd, edt_pickle)
    assert proc.returncode != 0
    if "undefined reference" not in proc.stderr and "undefined symbol" not in proc.stderr:
        pytest.skip(f"unsupported linker error format: {proc.stderr[:200]}")
    assert "DT Doctor" in proc.stdout
    assert "is enabled but no driver" in proc.stdout


def test_wrapper_diagnoses_unexpanded_macro_link_error(edt_pickle, cc, tmp_path):
    # An unexpanded macro cannot survive to link time through the real headers
    # (the compile fails first), so this leg stays synthetic by construction
    symbol = device_symbol("DT_N_ALIAS_dtdoctor_no_such_alias")
    bad_c = tmp_path / "bad_link.c"
    bad_c.write_text(
        f"extern int {symbol};\nint main(void) {{ return {symbol}; }}\n", encoding="utf-8"
    )
    obj = tmp_path / "bad_link.o"
    subprocess.run([cc, "-c", str(bad_c), "-o", str(obj)], check=True)

    link_cmd = [cc, str(obj), "-nostdlib", "-o", str(tmp_path / "bad_link.elf")]
    proc = run_wrapper_around(link_cmd, edt_pickle)
    assert proc.returncode != 0
    if "undefined reference" not in proc.stderr and "undefined symbol" not in proc.stderr:
        pytest.skip(f"unsupported linker error format: {proc.stderr[:200]}")
    assert "DT Doctor" in proc.stdout
    assert "No alias 'dtdoctor_no_such_alias' is defined" in proc.stdout
