# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""
Unit tests for the missing module diagnostics in kconfig.py.

The trees below mirror modules/Kconfig: the glue of every module is sourced
under 'if 0' so that its symbols stay defined, and the glue of an available
module is sourced again, next to the presence symbol scripts/zephyr_module.py
generates for it.
"""

import os

import kconfig
import pytest
from kconfiglib import Kconfig

TOP = """\
osource "modules.gen"

source "modules/Kconfig.bar"
source "modules/lv/Kconfig"

config SOC
	bool "SoC that selects a capability from the foo glue"
	select HAS_FOO

config DRIVER
	bool "Driver built from the bar module"
	depends on ZEPHYR_BAR_MODULE

if 0
osource "modules/*/Kconfig"
endif
"""

GLUE = {
    "modules/foo/Kconfig": """\
config ZEPHYR_FOO_MODULE
	bool

config FOO
	bool "Foo support"

config HAS_FOO
	bool
""",
    "modules/Kconfig.bar": """\
config ZEPHYR_BAR_MODULE
	bool
""",
    "modules/lv/Kconfig": """\
config ZEPHYR_LV_MODULE
	bool

config LV
	bool "LV support"

if LV

config LV_OPTION
	bool

endif
""",
}


def write_conf(path, conf):
    """Writes 'conf', a dict mapping symbol names to values, as assignments."""
    path.write_text("".join(f"CONFIG_{name}={value}\n" for name, value in conf.items()))


def configure(tmp_path, monkeypatch, conf, available=()):
    """Parses the tree with the 'available' modules and loads 'conf'."""
    monkeypatch.setenv("srctree", str(tmp_path))
    monkeypatch.setattr(kconfig, "warnings", [])

    (tmp_path / "Kconfig").write_text(TOP)
    for path, text in GLUE.items():
        (tmp_path / path).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / path).write_text(text)

    gen = ""
    for module in available:
        gen += f'config ZEPHYR_{module.upper()}_MODULE\n\tbool\n\tdefault y\n'
        if module == "foo":
            gen += 'osource "modules/foo/Kconfig"\n'
    (tmp_path / "modules.gen").write_text(gen)

    write_conf(tmp_path / "prj.conf", conf)
    kconf = Kconfig("Kconfig", warn_to_stderr=False)
    kconf.load_config(os.path.join(tmp_path, "prj.conf"))
    return kconf, kconfig.module_glue(kconf)


def test_glue_names_the_module(tmp_path, monkeypatch):
    _, glue = configure(tmp_path, monkeypatch, {}, available=("bar",))
    assert glue == {
        "modules/foo": ("foo", False),
        "modules/Kconfig.bar": ("bar", True),
        "modules/lv": ("lv", False),
    }


def test_option_of_missing_module(tmp_path, monkeypatch):
    kconf, glue = configure(tmp_path, monkeypatch, {"FOO": "y"})
    assert kconfig.check_assigned_sym_values(kconf, glue) == {"foo": ["FOO"]}

    assert len(kconfig.warnings) == 1
    warning = kconfig.warnings[0]
    assert "FOO needs the foo module, which is not available." in warning
    # The 'if 0' the glue is sourced under is not a dependency to check
    assert "unsatisfied" not in warning


def test_option_of_available_module(tmp_path, monkeypatch):
    kconf, glue = configure(tmp_path, monkeypatch, {"FOO": "y"}, available=("foo",))
    assert kconfig.check_assigned_sym_values(kconf, glue) == {}
    assert kconfig.warnings == []


def test_disabled_option_of_missing_module(tmp_path, monkeypatch):
    kconf, glue = configure(tmp_path, monkeypatch, {"FOO": "n"})
    assert kconfig.check_assigned_sym_values(kconf, glue) == {}
    assert kconfig.warnings == []


def test_board_defconfig_option_of_missing_module(tmp_path, monkeypatch):
    kconf, glue = configure(tmp_path, monkeypatch, {})
    write_conf(tmp_path / "board_defconfig", {"FOO": "y"})
    kconf.load_config(str(tmp_path / "board_defconfig"))

    assert kconfig.check_assigned_sym_values(kconf, glue) == {}
    assert len(kconfig.warnings) == 1
    warning = kconfig.warnings[0]
    assert "FOO needs the foo module, which is not available." in warning


def test_dependency_on_missing_module(tmp_path, monkeypatch):
    kconf, glue = configure(tmp_path, monkeypatch, {"DRIVER": "y"})
    assert kconfig.check_assigned_sym_values(kconf, glue) == {"bar": ["DRIVER"]}

    assert len(kconfig.warnings) == 1
    warning = kconfig.warnings[0]
    assert "ZEPHYR_BAR_MODULE (=n)" in warning
    assert "DRIVER needs the bar module, which is not available." in warning


def test_selected_symbol_of_missing_module(tmp_path, monkeypatch):
    kconf, glue = configure(tmp_path, monkeypatch, {"SOC": "y"})
    kconf.write_config(os.devnull)

    assert kconf.warnings
    assert kconfig.selected_missing_modules(kconf, glue) == {"foo": ["HAS_FOO"]}


def test_selected_symbol_of_available_module(tmp_path, monkeypatch):
    kconf, glue = configure(tmp_path, monkeypatch, {"SOC": "y"}, available=("foo",))
    kconf.write_config(os.devnull)

    assert kconf.warnings == []
    assert kconfig.selected_missing_modules(kconf, glue) == {}


def test_promptless_option_of_missing_module(tmp_path, monkeypatch):
    kconf, glue = configure(tmp_path, monkeypatch, {"LV": "y", "LV_OPTION": "y"})

    with pytest.raises(SystemExit) as e:
        kconfig.check_no_promptless_assign(kconf, glue)
    assert "needs the lv module, which is not available" in " ".join(str(e.value).split())


def test_promptless_option_of_available_module(tmp_path, monkeypatch):
    kconf, glue = configure(tmp_path, monkeypatch, {"LV": "y", "LV_OPTION": "y"}, available=("lv",))

    with pytest.raises(SystemExit) as e:
        kconfig.check_no_promptless_assign(kconf, glue)
    message = " ".join(str(e.value).split())
    assert "not directly user-configurable" in message
    assert "lv module" not in message


def run_main(tmp_path, monkeypatch, conf, available=()):
    """Runs kconfig.py on 'conf' the way the build does for a prj.conf."""
    configure(tmp_path, monkeypatch, conf, available)
    files = [str(tmp_path / name) for name in (".config", "autoconf.h", "sources.txt", "prj.conf")]
    argv = ["kconfig.py", "--handwritten-input-configs", "Kconfig", *files]
    monkeypatch.setattr("sys.argv", argv)
    kconfig.main()


def test_main_stops_on_option_of_missing_module(tmp_path, monkeypatch, capsys):
    with pytest.raises(SystemExit) as e:
        run_main(tmp_path, monkeypatch, {"FOO": "y"})

    assert "Aborting due to missing modules" in str(e.value)
    assert "The foo module is not available, but FOO needs it." in capsys.readouterr().err
    assert not (tmp_path / ".config").exists()


def test_main_writes_configuration_with_modules_available(tmp_path, monkeypatch):
    run_main(tmp_path, monkeypatch, {"FOO": "y"}, available=("foo",))

    assert "CONFIG_FOO=y" in (tmp_path / ".config").read_text()
