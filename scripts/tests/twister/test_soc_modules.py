# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""
Tests for the modules a build can use with --strict-soc-modules.
"""

from types import SimpleNamespace

import pytest
from twisterlib import soc_modules

SOC_YML = """\
family:
  - name: fam
    modules:
      - hal_fam
    series:
      - name: ser
        socs:
          - name: soc_a
            cpuclusters:
              - name: core1
                modules:
                  - hal_core1
              - name: core2
socs:
  - name: soc_b
    modules:
      - hal_b
"""

MODULES = ["hal_fam", "hal_core1", "hal_b", "lib"]


@pytest.fixture
def soc_root(tmp_path, monkeypatch):
    (tmp_path / "soc" / "vendor").mkdir(parents=True)
    (tmp_path / "soc" / "vendor" / "soc.yml").write_text(SOC_YML)
    modules = [SimpleNamespace(project=f"/modules/{m}", meta={"name": m}) for m in MODULES]
    monkeypatch.setattr(soc_modules, "_parse_modules", lambda: modules)
    soc_modules._socs.cache_clear()
    return [tmp_path]


def names(paths):
    return sorted(path.rsplit("/", 1)[-1] for path in paths)


@pytest.mark.parametrize(
    "platform, test_modules, visible",
    [
        ("board/soc_a/core1", [], ["hal_core1", "hal_fam", "lib"]),
        ("board/soc_a/core2", [], ["hal_fam", "lib"]),
        ("board@1.0.0/soc_b/ns", [], ["hal_b", "lib"]),
        ("board/soc_a/core2", ["hal_b"], ["hal_b", "hal_fam", "lib"]),
        ("board/unlisted_soc", [], ["lib"]),
    ],
)
def test_visible_modules(soc_root, platform, test_modules, visible):
    assert names(soc_modules.visible_modules(platform, test_modules, soc_root)) == visible
