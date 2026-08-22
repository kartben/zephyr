# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import argparse
import sys
from pathlib import Path

import pytest
import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import list_hardware
import list_modules

SOC_YML = """
modules:
  - cmsis
family:
  - name: demo_family
    modules:
      - family_mod
    series:
      - name: demo_series
        modules:
          - series_mod
        socs:
          - name: demo_soc
            modules:
              - soc_mod
          - name: other_soc
"""

BOARD_YML = """
board:
  name: demo_board
  full_name: Demo Board
  vendor: demo
  socs:
    - name: demo_soc
  modules:
    - board_mod
"""

SHIELD_YML = """
shield:
  name: demo_shield
  full_name: Demo Shield
  vendor: demo
  modules:
    - shield_mod
"""


def _write_tree(tmp_path: Path):
    soc_yml = tmp_path / "soc" / "demo" / "soc.yml"
    soc_yml.parent.mkdir(parents=True)
    soc_yml.write_text(SOC_YML)

    board_yml = tmp_path / "boards" / "demo" / "demo_board" / "board.yml"
    board_yml.parent.mkdir(parents=True)
    board_yml.write_text(BOARD_YML)

    shield_yml = tmp_path / "boards" / "shields" / "demo_shield" / "shield.yml"
    shield_yml.parent.mkdir(parents=True)
    shield_yml.write_text(SHIELD_YML)

    defaults = tmp_path / "defaults.yml"
    defaults.write_text("defaults:\n  - picolibc\n")
    return defaults


def test_soc_module_inheritance(tmp_path):
    _write_tree(tmp_path)
    systems = list_hardware.find_v2_systems(argparse.Namespace(soc_roots=[tmp_path]))
    demo = systems.get_soc("demo_soc")
    other = systems.get_soc("other_soc")
    assert demo.modules == ["cmsis", "family_mod", "series_mod", "soc_mod"]
    assert other.modules == ["cmsis", "family_mod", "series_mod"]


def test_resolve_board_and_shield(tmp_path):
    defaults = _write_tree(tmp_path)
    resolved = list_modules.resolve_modules(
        board_name="demo_board",
        shields=["demo_shield"],
        board_roots=[tmp_path],
        soc_roots=[tmp_path],
        include_defaults=True,
        defaults_file=defaults,
        zephyr_base=tmp_path,
    )
    assert resolved.names == [
        "picolibc",
        "board_mod",
        "cmsis",
        "family_mod",
        "series_mod",
        "soc_mod",
        "shield_mod",
    ]
    sources = {req.name: req.sources for req in resolved.required}
    assert "soc:demo_soc" in sources["cmsis"]
    assert "board:demo_board" in sources["board_mod"]
    assert "shield:demo_shield" in sources["shield_mod"]


def test_resolve_board_target_strips_qualifiers(tmp_path):
    defaults = _write_tree(tmp_path)
    resolved = list_modules.resolve_modules(
        board_name="demo_board/demo_soc",
        board_roots=[tmp_path],
        soc_roots=[tmp_path],
        include_defaults=False,
        defaults_file=defaults,
        zephyr_base=tmp_path,
    )
    assert "board_mod" in resolved.names
    assert "picolibc" not in resolved.names


def test_missing_uses_sanitized_names():
    resolved = list_modules.ResolvedModules(
        required=[
            list_modules.ModuleRequirement("cmsis-dsp"),
            list_modules.ModuleRequirement("hal_stm32"),
        ]
    )
    assert resolved.missing({"CMSIS_DSP"}) == ["hal_stm32"]
    assert list_modules.sanitize_module_name("tf-m-tests") == "TF_M_TESTS"


def test_unknown_board_raises(tmp_path):
    _write_tree(tmp_path)
    with pytest.raises(RuntimeError, match="not found"):
        list_modules.resolve_modules(
            board_name="no_such_board",
            board_roots=[tmp_path],
            soc_roots=[tmp_path],
            include_defaults=False,
            zephyr_base=tmp_path,
        )


def test_real_stm32_metadata_roundtrip():
    zephyr = Path(__file__).resolve().parents[2]
    resolved = list_modules.resolve_modules(
        board_name="nucleo_f401re",
        include_defaults=True,
        zephyr_base=zephyr,
    )
    assert "hal_stm32" in resolved.names
    assert "cmsis" in resolved.names
    assert "picolibc" in resolved.names


def test_real_shield_adds_sensor_hal():
    zephyr = Path(__file__).resolve().parents[2]
    resolved = list_modules.resolve_modules(
        board_name="nucleo_f401re",
        shields=["x_nucleo_iks01a3"],
        include_defaults=False,
        zephyr_base=zephyr,
    )
    assert "hal_st" in resolved.names
    assert "hal_stm32" in resolved.names


def test_annotated_soc_yml_validates():
    zephyr = Path(__file__).resolve().parents[2]
    soc_yml = zephyr / "soc" / "st" / "stm32" / "soc.yml"
    data = yaml.safe_load(soc_yml.read_text(encoding="utf-8"))
    assert "hal_stm32" in data["modules"]
    errors = list(list_hardware.soc_validator.iter_errors(data))
    assert errors == []
