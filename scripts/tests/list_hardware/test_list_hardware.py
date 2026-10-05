# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""
Unit tests for the modules a SoC needs, as list_hardware.py reads them from soc.yml.
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parents[2]))

import list_hardware  # noqa: E402

SOC_YML = """\
family:
  - name: fam
    modules:
      - hal_fam
    series:
      - name: ser
        modules:
          - hal_ser
          - hal_fam
        socs:
          - name: soc_in_series
            modules:
              - hal_soc
          - name: soc_with_clusters
            cpuclusters:
              - name: a
                modules:
                  - hal_a
              - name: b
    socs:
      - name: soc_in_family
series:
  - name: lone_series
    modules:
      - hal_lone
    socs:
      - name: soc_in_lone_series
socs:
  - name: lone_soc
    modules:
      - hal_own
  - name: plain_soc
"""


def socs():
    return {soc.name: soc for soc in list_hardware.Systems.from_yaml(SOC_YML).get_socs()}


@pytest.mark.parametrize(
    "soc, modules",
    [
        ("soc_in_series", ["hal_fam", "hal_ser", "hal_soc"]),
        ("soc_with_clusters", ["hal_fam", "hal_ser"]),
        ("soc_in_family", ["hal_fam"]),
        ("soc_in_lone_series", ["hal_lone"]),
        ("lone_soc", ["hal_own"]),
        ("plain_soc", []),
    ],
)
def test_soc_needs_the_modules_of_its_family_and_series(soc, modules):
    assert socs()[soc].modules == modules


def test_family_and_series_keep_their_own_modules():
    systems = list_hardware.Systems.from_yaml(SOC_YML)
    assert [f.modules for f in systems.get_families()] == [["hal_fam"]]
    assert {s.name: s.modules for s in systems.get_series()} == {
        "ser": ["hal_ser", "hal_fam"],
        "lone_series": ["hal_lone"],
    }


def test_cpucluster_keeps_its_own_modules():
    soc = socs()["soc_with_clusters"]
    assert soc.cpuclusters == ["a", "b"]
    assert soc.cpucluster_modules == {"a": ["hal_a"]}


def test_extension_adds_cpucluster_modules():
    systems = list_hardware.Systems.from_yaml(SOC_YML)
    systems.extend(
        list_hardware.Systems.from_yaml(
            "socs:\n"
            "  - extend: soc_with_clusters\n"
            "    cpuclusters:\n"
            "      - name: c\n"
            "        modules: [hal_c]\n"
        )
    )
    soc = next(s for s in systems.get_socs() if s.name == "soc_with_clusters")
    assert soc.cpuclusters == ["a", "b", "c"]
    assert soc.cpucluster_modules == {"a": ["hal_a"], "c": ["hal_c"]}


def test_module_listed_twice_is_rejected():
    with pytest.raises(SystemExit, match="Malformed soc YAML"):
        list_hardware.Systems.from_yaml("socs:\n  - name: s\n    modules: [a, a]\n")


@pytest.mark.parametrize(
    "soc, line",
    [
        ("soc_in_series", "NAME;soc_in_series;MODULES;hal_fam;hal_ser;hal_soc;CPUCLUSTER_MODULES;"),
        (
            "soc_with_clusters",
            "NAME;soc_with_clusters;MODULES;hal_fam;hal_ser;CPUCLUSTER_MODULES;a=hal_a",
        ),
    ],
)
def test_cmake_format_lists_the_modules(tmp_path, capsys, soc, line):
    soc_dir = tmp_path / "soc" / "vendor"
    soc_dir.mkdir(parents=True)
    (soc_dir / "soc.yml").write_text(SOC_YML)

    parser = list_hardware.argparse.ArgumentParser(allow_abbrev=False)
    list_hardware.add_args(parser)
    args = parser.parse_args(
        [
            f"--soc-root={tmp_path}",
            f"--soc={soc}",
            "--cmakeformat={NAME};{MODULES};{CPUCLUSTER_MODULES}",
        ]
    )
    list_hardware.dump_v2_systems(args)

    assert capsys.readouterr().out == line + "\n"
