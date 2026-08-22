# Copyright (c) 2026 Zephyr Project members and individual contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for scripts/module_dependency_audit.py."""

import textwrap

import module_dependency_audit as audit_tool
import pytest
from conftest import write_tree

# A workspace with one module used correctly, one used behind the scenes, and
# one that is nothing but tooling.
WORKSPACE = {
    "zephyr/west.yml": """\
        manifest:
          projects:
            - name: hal_tdk
              path: modules/hal/tdk
            - name: hal_vendor
              path: modules/hal/vendor
            - name: liblc3
              path: modules/lib/liblc3
            - name: bsim
              path: tools/bsim
              groups: [tools]
        """,
    # Zephyr's in-tree glue for the modules. Symbols under the module's
    # presence symbol cannot be set without the module; the others can.
    "zephyr/modules/hal_tdk/Kconfig": """\
        config ZEPHYR_HAL_TDK_MODULE
        \tbool

        config TDK_LOW_LEVEL_DRIVER
        \tbool
        \tdepends on ZEPHYR_HAL_TDK_MODULE

        if TDK_LOW_LEVEL_DRIVER

        config TDK_FIFO_SUPPORT
        \tbool

        endif
        """,
    "zephyr/modules/hal_vendor/Kconfig": """\
        config ZEPHYR_HAL_VENDOR_MODULE
        \tbool

        config HAS_VENDOR_HAL
        \tbool
        """,
    "zephyr/modules/liblc3/Kconfig": """\
        config ZEPHYR_LIBLC3_MODULE
        \tbool

        config LIBLC3
        \tbool "liblc3 support"
        \tdepends on ZEPHYR_LIBLC3_MODULE
        """,
    # Glue that never names its module, so nothing can be attributed to it.
    "zephyr/modules/Kconfig.orphan": """\
        config HAS_ORPHAN_SDK
        \tbool
        """,
    "zephyr/modules/Kconfig": """\
        comment "hal_vendor module not available."
        \tdepends on !ZEPHYR_HAL_VENDOR_MODULE
        """,
    # A driver that declares what it needs.
    "zephyr/drivers/sensor/tdk/Kconfig": """\
        config ICM42X70
        \tbool "TDK ICM42X70"
        \tdepends on ZEPHYR_HAL_TDK_MODULE
        """,
    "zephyr/drivers/sensor/tdk/CMakeLists.txt": """\
        zephyr_library_include_directories(${ZEPHYR_HAL_TDK_MODULE_DIR}/icm42x70)
        """,
    # A driver that does not.
    "zephyr/drivers/sensor/vendor/Kconfig": """\
        config VENDOR_SENSOR
        \tbool "Vendor sensor"
        """,
    # A driver that asks for the module's option without requiring the module.
    "zephyr/drivers/sensor/tdk_lite/Kconfig": """\
        config TDK_LITE
        \tbool "TDK lite"
        \tselect TDK_LOW_LEVEL_DRIVER
        """,
    "zephyr/drivers/sensor/vendor/CMakeLists.txt": """\
        zephyr_library_include_directories(${ZEPHYR_HAL_VENDOR_MODULE_DIR}/sensor)
        """,
    # A SoC that gates itself on a symbol that requires the module, and one
    # that only asks for the vendor HAL through an ungated capability symbol.
    "zephyr/soc/vendor/Kconfig": """\
        config SOC_SERIES_VENDOR_MOTION
        \tbool
        \tdepends on TDK_FIFO_SUPPORT

        config SOC_SERIES_VENDOR
        \tbool
        \tselect HAS_VENDOR_HAL
        """,
    # A subsystem that names a module in help text and in a comment only.
    "zephyr/subsys/audio/Kconfig": """\
        config AUDIO_CODEC
        \tbool "Audio codec"
        \thelp
        \t  Needs LIBLC3 when the LC3 codec is used.

        # LIBLC3 is set up elsewhere.
        """,
}


@pytest.fixture
def workspace(tmp_path):
    """A miniature Zephyr workspace, with no module checked out."""
    write_tree(tmp_path, WORKSPACE)
    return tmp_path / "zephyr"


@pytest.fixture
def modules(workspace):
    return audit_tool.audit(workspace, workspace / "west.yml", sources=False)


def test_a_declared_dependency_is_recognized(modules):
    """'depends on ZEPHYR_HAL_TDK_MODULE' is what a declared dependency looks like."""
    declared = modules["hal_tdk"].declared

    assert ("kconfig", "drivers/sensor/tdk/Kconfig") in {(ref.kind, ref.path) for ref in declared}


def test_a_module_path_used_without_a_declaration_is_undeclared(modules):
    """A CMake reference into a module is a dependency the build model misses."""
    vendor = modules["hal_vendor"]

    assert ("cmake", "drivers/sensor/vendor") in {(ref.kind, ref.consumer)
                                                  for ref in vendor.undeclared}
    assert "drivers/sensor/vendor" in vendor.undeclared_consumers
    assert vendor.candidate == "no"


def test_a_declaring_consumer_may_use_the_module_path(modules):
    """The Kconfig dependency already gates the directory's CMake code."""
    tdk = modules["hal_tdk"]

    assert ("cmake", "drivers/sensor/tdk/CMakeLists.txt") in {
        (ref.kind, ref.path) for ref in tdk.undeclared}
    assert "drivers/sensor/tdk" not in tdk.undeclared_consumers


def test_depending_on_a_module_conditional_symbol_declares_the_dependency(modules):
    """A symbol that cannot be set without the module gates whoever depends on it."""
    tdk = modules["hal_tdk"]

    # TDK_FIFO_SUPPORT inherits the gate from the 'if' block it sits in.
    assert tdk.conditional_symbols == ["TDK_FIFO_SUPPORT", "TDK_LOW_LEVEL_DRIVER",
                                       "ZEPHYR_HAL_TDK_MODULE_BLOBS"]
    assert ("proxy", "soc/vendor/Kconfig") in {(ref.kind, ref.path) for ref in tdk.declared}


def test_an_ungated_glue_symbol_declares_nothing(modules):
    """A capability symbol anyone can set does not make the build need the module."""
    vendor = modules["hal_vendor"]

    assert "HAS_VENDOR_HAL" not in vendor.conditional_symbols
    assert ("capability", "soc/vendor/Kconfig") in {(ref.kind, ref.path) for ref in
                                                    vendor.undeclared}
    assert "soc/vendor" in vendor.undeclared_consumers


def test_selecting_a_module_symbol_does_not_declare_the_dependency(modules):
    """A select is simply lost when the module is inactive, so it gates nothing."""
    tdk = modules["hal_tdk"]

    assert ("select", "drivers/sensor/tdk_lite/Kconfig") in {
        (ref.kind, ref.path) for ref in tdk.undeclared}
    assert "drivers/sensor/tdk_lite" in tdk.undeclared_consumers


def test_reacting_to_an_absent_module_is_not_a_dependency(modules):
    """'depends on !ZEPHYR_<MODULE>_MODULE' is the opposite of needing it."""
    assert modules["hal_vendor"].declared == []


def test_a_tab_after_if_still_gates(tmp_path):
    """Kconfig accepts 'if\\tSYMBOL'; the gating analysis must too."""
    write_tree(tmp_path, {
        "zephyr/west.yml": "manifest:\n  projects:\n    - name: hal_tab\n"
                           "      path: modules/hal/tab\n",
        "zephyr/modules/hal_tab/Kconfig": (
            "config ZEPHYR_HAL_TAB_MODULE\n\tbool\n\n"
            "config HAS_TAB_HAL\n\tbool\n\tdepends on ZEPHYR_HAL_TAB_MODULE\n\n"
            "if\tHAS_TAB_HAL\n\nconfig TAB_UART\n\tbool\n\nendif\n"),
    })

    modules = audit_tool.audit(tmp_path / "zephyr", tmp_path / "zephyr" / "west.yml",
                               sources=False)

    assert "TAB_UART" in modules["hal_tab"].conditional_symbols


def test_glue_that_names_no_module_is_reported(workspace, modules):
    """Glue has to declare its module's presence symbol to be attributable."""
    unattributed = audit_tool.unattributed_glue(audit_tool.read_tree(workspace), modules)

    assert unattributed == {"modules/Kconfig.orphan": ["HAS_ORPHAN_SDK"]}


def test_help_text_and_comments_are_not_uses(modules):
    """A module named in prose is not a dependency."""
    assert modules["liblc3"].references == []
    assert modules["liblc3"].classification == "no-evidence"
    # Absence of evidence is not evidence of absence.
    assert modules["liblc3"].candidate == "no"


def test_a_module_own_glue_is_not_a_consumer(modules):
    """What a module's glue says about the module describes it, not uses of it."""
    assert all(not ref.path.startswith("modules/hal_tdk/")
               for ref in modules["hal_tdk"].references)


def test_tooling_projects_are_out_of_scope(modules):
    """Projects that are not part of firmware builds need no dependency work."""
    assert modules["bsim"].classification == "tooling"
    assert modules["bsim"].candidate == "n/a"


def test_modules_are_classified_by_who_uses_them(modules):
    """What a module is to the build is what most of its users make of it."""
    assert modules["hal_tdk"].classification == "driver"
    assert modules["hal_vendor"].classification == "platform"


def test_a_checked_out_module_provides_its_own_identity(workspace):
    """module.yml decides the module's name and what metadata it contributes."""
    write_tree(workspace.parent, {
        "modules/hal/vendor/zephyr/module.yml": """\
            name: hal_vendor
            build:
              cmake: zephyr
              depends:
                - hal_tdk
              settings:
                soc_root: .
            """,
    })

    modules = audit_tool.audit(workspace, workspace / "west.yml", sources=False)

    assert modules["hal_vendor"].available
    assert modules["hal_vendor"].depends == ["hal_tdk"]
    assert modules["hal_vendor"].metadata_roots == ["soc_root"]
    # Build metadata has to be there before dependency resolution can run.
    assert modules["hal_vendor"].classification == "bootstrap"
    assert "provides build metadata" in modules["hal_vendor"].blockers


def test_sources_including_a_module_header_are_found(workspace):
    """With the module checked out, C code can be attributed to it too."""
    write_tree(workspace.parent, {
        "modules/hal/vendor/include/vendor_hal.h": "",
        "modules/hal/vendor/zephyr/module.yml": "name: hal_vendor\n",
    })
    write_tree(workspace, {"drivers/misc/vendor.c": '#include <vendor_hal.h>\n'})

    modules = audit_tool.audit(workspace, workspace / "west.yml")
    vendor = modules["hal_vendor"]

    assert ("source", "drivers/misc/vendor.c") in {
        (ref.kind, ref.path) for ref in vendor.undeclared}
    assert "drivers/misc" in vendor.undeclared_consumers


def test_a_header_zephyr_also_provides_identifies_nothing(workspace):
    """An include that resolves inside Zephyr must not be blamed on a module."""
    write_tree(workspace.parent, {"modules/hal/vendor/include/kernel.h": ""})
    write_tree(workspace, {
        "include/zephyr/kernel.h": "",
        "drivers/misc/vendor.c": "#include <zephyr/kernel.h>\n",
    })

    modules = audit_tool.audit(workspace, workspace / "west.yml")

    assert all(ref.kind != "source" for ref in modules["hal_vendor"].references)


def test_symbols_no_project_provides_are_reported(workspace):
    """A module symbol nothing in the manifest backs is a dangling reference."""
    write_tree(workspace, {"drivers/misc/Kconfig": "\tdepends on ZEPHYR_GONE_MODULE\n"})
    modules = audit_tool.audit(workspace, workspace / "west.yml", sources=False)

    unknown = audit_tool.unknown_symbols(audit_tool.read_tree(workspace), modules)

    assert unknown["GONE"] == {"drivers/misc/Kconfig"}


def test_the_inventory_is_machine_readable(modules):
    entry = modules["hal_vendor"].as_dict()

    assert entry["name"] == "hal_vendor"
    assert entry["symbol"] == "ZEPHYR_HAL_VENDOR_MODULE"
    assert "drivers/sensor/vendor" in entry["undeclared_consumers"]
    assert entry["candidate"] == "no"


def test_the_table_lists_every_module(modules, capsys):
    audit_tool.print_table(sorted(modules.values(), key=lambda m: m.name))

    table = capsys.readouterr().out

    assert "hal_tdk" in table
    assert "hal_vendor" in table
    assert "4 modules" in table


def test_the_tree_scan_matches_a_real_zephyr_layout():
    """The regexes must not drift from what the Zephyr tree actually contains."""
    lines = textwrap.dedent("""\
        config ICM42X70
        \tdepends on ZEPHYR_HAL_TDK_MODULE
        """).splitlines()

    references = [
        ref for line in lines
        for ref in audit_tool._line_references(line, is_kconfig=True, symbols={})
    ]

    assert references == [("HAL_TDK", "kconfig")]
