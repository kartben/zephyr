# Copyright (c) 2026 Zephyr Project members and individual contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for scripts/kconfig/module_requirements.py."""

import json

import module_requirements
import pytest
import zephyr_module
from conftest import write_tree
from kconfiglib import Kconfig

MODULES = ["hal_tdk", "hal_nxp", "cmsis"]


def presence(modules, absent=()):
    """The presence symbols zephyr_module.py generates for a workspace.

    They exist whether or not the module is there, and 'default y' is what
    says it is: the analysis reads a configuration that has its modules, so
    the trees below have to be configured that way too.
    """
    blocks = []
    for name in modules:
        block = f"    config ZEPHYR_{module_requirements.sanitize(name)}_MODULE\n    \tbool\n"
        if name not in absent:
            block += "    \tdefault y\n"
        blocks.append(block)
    return "\n".join(blocks) + "\n"


def enabled(symbol):
    """An application configuration enabling one symbol.

    Built rather than written out, so that the symbols these fixtures invent
    do not read as references to undefined Kconfig symbols when the tree is
    scanned for them.
    """
    return f"CONFIG_{symbol}=y\n"


@pytest.fixture
def analyze(tmp_path, monkeypatch):
    """Analyze a Kconfig tree, optionally with a configuration applied."""

    def run(kconfig, config=None, modules=None, absent=()):
        modules = modules or MODULES
        write_tree(tmp_path, {"Kconfig": presence(modules, absent) + kconfig})
        monkeypatch.chdir(tmp_path)
        kconf = Kconfig("Kconfig", warn_to_stderr=False)
        if config:
            write_tree(tmp_path, {"prj.conf": config})
            kconf.load_config("prj.conf", replace=False)
        return module_requirements.required_modules(kconf, modules)

    return run


def test_a_feature_the_configuration_asks_for_requires_its_module(analyze):
    """CONFIG_FOO=y and 'depends on ZEPHYR_BAR_MODULE' means bar is needed."""
    required = analyze(
        """\
        config ICM42X70
        \tbool "TDK ICM42X70"
        \tdepends on ZEPHYR_HAL_TDK_MODULE
        """,
        config=enabled("ICM42X70"),
    )

    assert required == {"hal_tdk": ["ICM42X70"]}


def test_a_feature_nobody_asks_for_requires_nothing(analyze):
    """The module is only needed if something in this build wants the feature."""
    required = analyze("""\
        config ICM42X70
        \tbool "TDK ICM42X70"
        \tdepends on ZEPHYR_HAL_TDK_MODULE
        """)

    assert required == {}


def test_a_default_that_applies_requires_the_module(analyze):
    """A driver enabled by its devicetree node needs its HAL, unasked."""
    required = analyze("""\
        config DT_HAS_INVENSENSE_ICM42670P_ENABLED
        \tbool
        \tdefault y

        config ICM42X70
        \tbool "TDK ICM42X70"
        \tdefault y
        \tdepends on DT_HAS_INVENSENSE_ICM42670P_ENABLED
        \tdepends on ZEPHYR_HAL_TDK_MODULE
        """)

    assert required == {"hal_tdk": ["ICM42X70"]}


def test_a_default_that_does_not_apply_requires_nothing(analyze):
    """Without the devicetree node, the same driver needs nothing."""
    required = analyze("""\
        config DT_HAS_INVENSENSE_ICM42670P_ENABLED
        \tbool

        config ICM42X70
        \tbool "TDK ICM42X70"
        \tdefault y
        \tdepends on DT_HAS_INVENSENSE_ICM42670P_ENABLED
        \tdepends on ZEPHYR_HAL_TDK_MODULE
        """)

    assert required == {}


def test_a_default_the_configuration_turns_off_requires_nothing(analyze):
    """A user value replaces the defaults; it does not compete with them."""
    required = analyze(
        """\
        config ICM42X70
        \tbool "TDK ICM42X70"
        \tdefault y
        \tdepends on ZEPHYR_HAL_TDK_MODULE
        """,
        config="CONFIG_ICM42X70=n\n",
    )

    assert required == {}


def test_a_select_that_overrides_its_dependencies_requires_the_module(analyze):
    """Kconfig lets select win over depends on, and warns. So does this.

    The selected symbol ends up y with its dependencies unsatisfied, which is
    a build that needs the module more than most, not less: the module is
    what the unsatisfied dependency is asking for.
    """
    required = analyze(
        """\
        config BOARD_HAS_SENSOR
        \tbool

        config SENSOR_SHIM
        \tbool "Shim"
        \tselect TDK_DRIVER

        config TDK_DRIVER
        \tbool
        \tdepends on BOARD_HAS_SENSOR && ZEPHYR_HAL_TDK_MODULE
        """,
        config=enabled("SENSOR_SHIM"),
    )

    assert required == {"hal_tdk": ["TDK_DRIVER"]}


def test_a_selected_symbol_requires_its_module(analyze):
    """Reverse dependencies enable symbols too."""
    required = analyze(
        """\
        config SENSOR_SHIM
        \tbool "Shim"
        \tselect TDK_DRIVER

        config TDK_DRIVER
        \tbool
        \tdepends on ZEPHYR_HAL_TDK_MODULE
        """,
        config=enabled("SENSOR_SHIM"),
    )

    assert required == {"hal_tdk": ["TDK_DRIVER"]}


def test_an_implied_symbol_requires_its_module(analyze):
    required = analyze(
        """\
        config SENSOR_SHIM
        \tbool "Shim"
        \timply TDK_DRIVER

        config TDK_DRIVER
        \tbool
        \tdepends on ZEPHYR_HAL_TDK_MODULE
        """,
        config=enabled("SENSOR_SHIM"),
    )

    assert required == {"hal_tdk": ["TDK_DRIVER"]}


def test_every_module_a_symbol_cannot_do_without_is_required(analyze):
    """Two modules, both needed."""
    required = analyze(
        """\
        config FUSION
        \tbool "Fusion"
        \tdepends on ZEPHYR_HAL_TDK_MODULE && ZEPHYR_CMSIS_MODULE
        """,
        config=enabled("FUSION"),
    )

    assert required == {"cmsis": ["FUSION"], "hal_tdk": ["FUSION"]}


def test_alternatives_do_not_require_either_module(analyze):
    """With a choice of modules, the analysis does not pick one."""
    required = analyze(
        """\
        config CRYPTO_BACKEND
        \tbool "Crypto"
        \tdepends on ZEPHYR_HAL_TDK_MODULE || ZEPHYR_HAL_NXP_MODULE
        """,
        config=enabled("CRYPTO_BACKEND"),
    )

    assert required == {}


def test_a_module_that_must_be_absent_is_not_required(analyze):
    """'depends on !ZEPHYR_FOO_MODULE' is the opposite of needing foo."""
    tree = """\
        config FALLBACK
        \tbool "Fallback"
        \tdefault y
        \tdepends on !ZEPHYR_HAL_TDK_MODULE
        """

    # Without the module the fallback is on, and taking hal_tdk away is what
    # keeps it on, so it is not something the build needs.
    assert analyze(tree, absent=["hal_tdk"]) == {}
    # With the module there the fallback is off, so there is nothing to need.
    assert analyze(tree) == {}


def test_nested_conditions_are_evaluated(analyze):
    """The non-module part of the condition decides whether the module is needed."""
    tree = """\
        config BOARD_HAS_SENSOR
        \tbool
        \t%s

        config BUS_READY
        \tbool
        \tdefault y

        config SENSOR
        \tbool
        \tdefault y if BOARD_HAS_SENSOR && BUS_READY
        \tdepends on ZEPHYR_HAL_TDK_MODULE
        """

    assert analyze(tree % "default y") == {"hal_tdk": ["SENSOR"]}
    assert analyze(tree % "bool") == {}


def test_a_symbol_blocked_by_something_else_requires_nothing(analyze):
    """A feature that could not be enabled anyway needs no module."""
    required = analyze("""\
        config POWER_DOMAIN
        \tbool

        config ICM42X70
        \tbool "TDK ICM42X70"
        \tdefault y
        \tdepends on POWER_DOMAIN
        \tdepends on ZEPHYR_HAL_TDK_MODULE
        """)

    assert required == {}


def test_requirements_follow_from_the_module_a_module_needs(analyze):
    """A module-conditional symbol that needs another module reports both."""
    required = analyze("""\
        config TDK_DSP_FUSION
        \tbool "TDK fusion using CMSIS DSP"
        \tdefault y
        \tdepends on ZEPHYR_HAL_TDK_MODULE
        \tdepends on ZEPHYR_CMSIS_MODULE
        """)

    assert required == {"cmsis": ["TDK_DSP_FUSION"], "hal_tdk": ["TDK_DSP_FUSION"]}


def test_module_identity_comes_from_the_module_list(analyze):
    """A sanitized symbol name is not turned back into a module name."""
    required = analyze(
        """\
        config LIB
        \tbool "Lib"
        \tdepends on ZEPHYR_LORA_BASICS_MODEM_MODULE
        """,
        config=enabled("LIB"),
        modules=["lora-basics-modem"],
    )

    assert required == {"lora-basics-modem": ["LIB"]}


def test_the_requirements_are_what_the_build_reads(tmp_path):
    """The output is the requirements file zephyr_module.py takes."""
    report = module_requirements.requirements_report({"hal_tdk": ["ICM42X70"]})

    assert report == {
        "schema_version": 1,
        "required": [{"name": "hal_tdk", "required_by": ["ICM42X70"]}],
    }

    # And it round-trips through the module list zephyr_module.py writes.
    modules = tmp_path / "modules.json"
    modules.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "modules": [{"name": "hal_tdk"}, {"name": "lora-basics-modem"}],
            }
        )
    )

    assert module_requirements.module_names_from_file(modules) == ["hal_tdk", "lora-basics-modem"]
    assert module_requirements.presence_symbols(["lora-basics-modem"]) == {
        "ZEPHYR_LORA_BASICS_MODEM_MODULE": "lora-basics-modem",
    }


def test_a_module_list_from_the_future_is_refused(tmp_path):
    modules = tmp_path / "modules.json"
    modules.write_text(json.dumps({"schema_version": 2, "modules": []}))

    with pytest.raises(SystemExit):
        module_requirements.module_names_from_file(modules)


def test_the_analysis_feeds_the_build(tmp_path, monkeypatch):
    """What this writes is what decides which modules a build activates."""
    write_tree(
        tmp_path,
        {
            "Kconfig": presence(MODULES) + "config ICM42X70\n\tbool\n\tdefault y\n"
            "\tdepends on ZEPHYR_HAL_TDK_MODULE\n",
            "zephyr/.config": "",
            "modules.json": json.dumps(
                {
                    "schema_version": 1,
                    "modules": [{"name": name} for name in MODULES],
                }
            ),
        },
    )
    monkeypatch.chdir(tmp_path)

    module_requirements.main(
        [
            "--modules-file",
            "modules.json",
            "--config",
            "zephyr/.config",
            "--out",
            "requirements.json",
            "Kconfig",
        ]
    )

    assert zephyr_module.read_requirements(tmp_path / "requirements.json") == {
        "hal_tdk": ["ICM42X70"],
    }
