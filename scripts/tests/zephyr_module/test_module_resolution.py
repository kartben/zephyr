# Copyright (c) 2026 Zephyr Project members and individual contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for the module resolution in scripts/zephyr_module.py.

The unit tests work on module descriptions directly. The workspace tests run
the script inside a real, fully populated west workspace, which is the case
that matters: strict activation only means something if having a module in the
workspace is not the same as using it.
"""

import json
import shutil
import subprocess
import sys

import pytest
import zephyr_module
from conftest import ManifestLab, write_tree

pytestmark = pytest.mark.skipif(shutil.which("west") is None, reason="west CLI not on PATH")


def module(name, path=None, depends=(), meta=None):
    """A module as parse_modules() would have returned it."""
    return zephyr_module.Module(
        project=path or f"modules/{name}",
        meta={"name": name, "name-sanitized": name, "build": dict(meta or {})},
        depends=list(depends),
    )


WORKSPACE_MODULES = [
    module("cmsis"),
    module("hal_tdk"),
    module("hal_vendor", depends=["cmsis"]),
    module("board_provider", meta={"settings": {"board_root": "."}}),
    module("bootloader", meta={"sysbuild-cmake": "sysbuild"}),
]


def active_names(resolution):
    return sorted(m.meta["name"] for m in resolution.active)


def test_every_module_is_active_by_default():
    """Zephyr has always built with every module in the workspace."""
    resolution = zephyr_module.resolve_modules(WORKSPACE_MODULES)

    assert active_names(resolution) == sorted(m.meta["name"] for m in WORKSPACE_MODULES)
    assert resolution.activation == zephyr_module.ACTIVATION_ALL


def test_strict_activation_leaves_unused_modules_out():
    """A module in the workspace that nothing needs is not part of the build."""
    resolution = zephyr_module.resolve_modules(
        WORKSPACE_MODULES,
        required={"hal_tdk": ["ICM42X70"]},
        activation=zephyr_module.ACTIVATION_STRICT,
    )

    assert "hal_tdk" in active_names(resolution)
    assert "cmsis" not in active_names(resolution)
    assert "hal_vendor" not in active_names(resolution)
    assert resolution.reasons["hal_tdk"] == zephyr_module.REASON_REQUIRED
    assert resolution.required_by["hal_tdk"] == ["ICM42X70"]


def test_strict_activation_follows_module_dependencies():
    """A module the build needs brings in the modules it needs itself."""
    resolution = zephyr_module.resolve_modules(
        WORKSPACE_MODULES,
        required={"hal_vendor": ["SOC_VENDOR"]},
        activation=zephyr_module.ACTIVATION_STRICT,
    )

    assert "cmsis" in active_names(resolution)
    assert resolution.reasons["cmsis"] == zephyr_module.REASON_DEPENDENCY
    assert resolution.required_by["cmsis"] == ["hal_vendor"]


def test_modules_the_user_supplies_are_deliberate():
    """Naming a module by hand activates it, whatever the build depends on."""
    resolution = zephyr_module.resolve_modules(
        WORKSPACE_MODULES, explicit=["modules/hal_tdk"], activation=zephyr_module.ACTIVATION_STRICT
    )

    assert "hal_tdk" in active_names(resolution)
    assert resolution.reasons["hal_tdk"] == zephyr_module.REASON_EXPLICIT


def test_modules_providing_build_metadata_stay_active():
    """Board, SoC and sysbuild metadata is needed before dependencies are known."""
    resolution = zephyr_module.resolve_modules(
        WORKSPACE_MODULES, activation=zephyr_module.ACTIVATION_STRICT
    )

    assert active_names(resolution) == ["board_provider", "bootloader"]
    assert resolution.reasons["board_provider"] == zephyr_module.REASON_METADATA
    assert resolution.reasons["bootloader"] == zephyr_module.REASON_METADATA


def test_a_required_module_that_is_not_there_is_reported():
    """The failure is a missing module, not a missing header."""
    resolution = zephyr_module.resolve_modules(
        WORKSPACE_MODULES,
        required={"hal_absent": ["ICM42X70"]},
        activation=zephyr_module.ACTIVATION_STRICT,
    )

    assert resolution.missing == {"hal_absent": ["ICM42X70"]}
    assert "is unavailable" in zephyr_module.missing_module_error(resolution.missing)
    assert "ICM42X70" in zephyr_module.missing_module_error(resolution.missing)


def test_the_resolution_is_machine_readable():
    resolution = zephyr_module.resolve_modules(
        WORKSPACE_MODULES,
        required={"hal_vendor": ["SOC_VENDOR"], "gone": ["SOC_VENDOR"]},
        activation=zephyr_module.ACTIVATION_STRICT,
    )

    report = zephyr_module.resolution_report(WORKSPACE_MODULES, resolution)
    modules = {entry["name"]: entry for entry in report["modules"]}

    assert report["schema_version"] == 1
    assert report["activation"] == "strict"
    assert modules["hal_vendor"] == {
        "name": "hal_vendor",
        "path": "modules/hal_vendor",
        "available": True,
        "active": True,
        "reason": "required",
        "required": True,
        "required_by": ["SOC_VENDOR"],
    }
    assert modules["hal_tdk"]["active"] is False
    assert modules["hal_tdk"]["reason"] is None
    assert report["missing"] == [{"name": "gone", "required_by": ["SOC_VENDOR"]}]


def test_an_unknown_activation_mode_is_refused():
    with pytest.raises(SystemExit):
        zephyr_module.resolve_modules(WORKSPACE_MODULES, activation="lenient")


def test_requirements_are_read_from_a_file(tmp_path):
    requirements = tmp_path / "requirements.json"
    requirements.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "required": [{"name": "hal_tdk", "required_by": ["ICM42X70"]}, {"name": "cmsis"}],
            }
        )
    )

    assert zephyr_module.read_requirements(requirements) == {
        "hal_tdk": ["ICM42X70"],
        "cmsis": [],
    }


def test_a_requirements_file_from_the_future_is_refused(tmp_path):
    requirements = tmp_path / "requirements.json"
    requirements.write_text(json.dumps({"schema_version": 2, "required": []}))

    with pytest.raises(SystemExit):
        zephyr_module.read_requirements(requirements)


# The workspace tests below build a west workspace holding every module, and
# then check what a build makes of it.

MODULE_YAML = {
    "hal_tdk": "name: hal_tdk\nbuild:\n  cmake: zephyr\n  kconfig: zephyr/Kconfig\n",
    "hal_nxp": "name: hal_nxp\nbuild:\n  cmake: zephyr\n  kconfig: zephyr/Kconfig\n",
    "cmsis": "name: cmsis\nbuild:\n  cmake: zephyr\n  kconfig: zephyr/Kconfig\n",
    "hal_vendor": (
        "name: hal_vendor\nbuild:\n  cmake: zephyr\n  kconfig: zephyr/Kconfig\n"
        "  depends:\n    - cmsis\n"
    ),
    "board_provider": (
        "name: board_provider\nbuild:\n  cmake: zephyr\n  settings:\n    board_root: .\n"
    ),
}


@pytest.fixture(scope="module")
def workspace(lab: ManifestLab):
    """A west workspace in which every module is present and active."""
    projects = []
    for name, module_yml in MODULE_YAML.items():
        files = lab.module_files(name)
        files["zephyr/module.yml"] = module_yml
        lab.add_repo(name, files)
        projects.append(
            f"    - name: {name}\n"
            f"      url: {lab.url(name)}\n"
            f"      revision: {lab.sha(name)}\n"
            f"      path: modules/{name}\n"
        )

    lab.add_repo("zephyr", {"west.yml": "manifest:\n  projects:\n" + "".join(projects)})
    topdir = lab.workspace("populated", "zephyr")
    subprocess.run(["west", "update"], cwd=topdir, check=True, capture_output=True)
    return topdir


def run_zephyr_module(workspace, tmp_path, *args):
    """Generate the build files for a workspace, and read them back."""
    outputs = {
        name: tmp_path / name
        for name in ("Kconfig.modules", "zephyr_modules.txt", "zephyr_settings.txt", "modules.json")
    }
    subprocess.run(
        [
            sys.executable,
            str(zephyr_module.__file__),
            "--zephyr-base",
            str(workspace / "zephyr"),
            "--kconfig-out",
            str(outputs["Kconfig.modules"]),
            "--cmake-out",
            str(outputs["zephyr_modules.txt"]),
            "--settings-out",
            str(outputs["zephyr_settings.txt"]),
            "--modules-out",
            str(outputs["modules.json"]),
            *args,
        ],
        cwd=workspace,
        check=True,
        capture_output=True,
        text=True,
    )
    return {name: path.read_text() for name, path in outputs.items()}


def test_a_populated_workspace_activates_everything_by_default(workspace, tmp_path):
    """Nothing changes for a build that does not ask for strict activation."""
    generated = run_zephyr_module(workspace, tmp_path)

    for name in MODULE_YAML:
        assert f'"{name}"' in generated["zephyr_modules.txt"]
        assert (
            f"config ZEPHYR_{name.upper()}_MODULE\n\tbool\n\tdefault y"
            in generated["Kconfig.modules"]
        )


def test_a_populated_workspace_does_not_hide_undeclared_dependencies(workspace, tmp_path):
    """The point of strict activation: present is not the same as used.

    hal_nxp is right there in the workspace, and a build that never says it
    needs it does not get it: no Kconfig symbol set, no module directory for
    CMake to reach into, so anything using it fails instead of quietly working.
    """
    generated = run_zephyr_module(
        workspace, tmp_path, "--activation", "strict", "--required", "hal_tdk"
    )

    assert "config ZEPHYR_HAL_TDK_MODULE\n\tbool\n\tdefault y" in generated["Kconfig.modules"]
    assert "config ZEPHYR_HAL_NXP_MODULE\n\tbool\n\n" in generated["Kconfig.modules"]

    assert '"hal_tdk"' in generated["zephyr_modules.txt"]
    assert "hal_nxp" not in generated["zephyr_modules.txt"]


def test_build_metadata_survives_strict_activation(workspace, tmp_path):
    """A board root has to be there before anything can depend on it."""
    generated = run_zephyr_module(workspace, tmp_path, "--activation", "strict")

    assert "BOARD_ROOT" in generated["zephyr_settings.txt"]
    assert '"board_provider"' in generated["zephyr_modules.txt"]


def test_module_dependencies_come_along(workspace, tmp_path):
    """cmsis is not required by the build, but hal_vendor cannot do without it."""
    generated = run_zephyr_module(
        workspace, tmp_path, "--activation", "strict", "--required", "hal_vendor"
    )
    report = json.loads(generated["modules.json"])
    modules = {entry["name"]: entry for entry in report["modules"]}

    assert modules["cmsis"]["active"] is True
    assert modules["cmsis"]["reason"] == "dependency"
    assert modules["cmsis"]["required_by"] == ["hal_vendor"]
    assert modules["hal_tdk"]["active"] is False


def test_a_module_supplied_by_hand_satisfies_the_requirement(workspace, tmp_path):
    """Modules do not have to come from west."""
    generated = run_zephyr_module(
        workspace,
        tmp_path,
        "--activation",
        "strict",
        "--required",
        "hal_tdk",
        "--extra-modules",
        str(workspace / "modules" / "hal_nxp"),
    )
    report = json.loads(generated["modules.json"])
    modules = {entry["name"]: entry for entry in report["modules"]}

    assert modules["hal_nxp"]["reason"] == "explicit"
    assert modules["hal_tdk"]["reason"] == "required"


def test_a_missing_module_fails_the_build_early(workspace, tmp_path):
    """Not a missing header, a missing module, named as such."""
    with pytest.raises(subprocess.CalledProcessError) as failure:
        run_zephyr_module(workspace, tmp_path, "--activation", "strict", "--required", "hal_absent")

    assert "required module 'hal_absent' is unavailable" in failure.value.stderr
    assert "EXTRA_ZEPHYR_MODULES" in failure.value.stderr


def test_module_dependencies_survive_parsing(workspace):
    """Resolution needs build.depends, which the topological sort used to eat."""
    modules = zephyr_module.parse_modules(
        str(workspace / "zephyr"),
        modules=[str(workspace / "modules" / name) for name in MODULE_YAML],
    )

    depends = {m.meta["name"]: m.depends for m in modules}

    assert depends["hal_vendor"] == ["cmsis"]


def test_requirements_flow_from_a_file_into_the_resolution(workspace, tmp_path):
    """The requirements file is how requirement analysis reaches the build."""
    write_tree(
        tmp_path,
        {
            "requirements.json": json.dumps(
                {
                    "schema_version": 1,
                    "required": [{"name": "hal_vendor", "required_by": ["SOC_SERIES_VENDOR"]}],
                }
            )
        },
    )

    generated = run_zephyr_module(
        workspace,
        tmp_path,
        "--activation",
        "strict",
        "--required-file",
        str(tmp_path / "requirements.json"),
    )
    report = json.loads(generated["modules.json"])
    modules = {entry["name"]: entry for entry in report["modules"]}

    assert modules["hal_vendor"]["required_by"] == ["SOC_SERIES_VENDOR"]
    assert modules["cmsis"]["active"] is True


def test_a_cloned_but_inactive_project_is_not_a_module(lab, tmp_path):
    """west treats inactive projects as absent, and so does module discovery.

    This is what makes moving a module to a disabled manifest group
    effective even in a fully populated workspace: fetching the
    repository is not the same as activating it. Re-enabling the module
    takes the group, not just the clone.
    """
    lab.add_repo("hal_opt")
    lab.add_repo(
        "zephyr-opt",
        {
            "west.yml": f"""\
manifest:
  group-filter: [-optional]
  projects:
    - name: hal_opt
      url: {lab.url('hal_opt')}
      revision: {lab.sha('hal_opt')}
      path: modules/hal_opt
      groups: [optional]
"""
        },
    )
    topdir = lab.workspace("group-flip", "zephyr-opt")
    subprocess.run(["west", "update", "hal_opt"], cwd=topdir, check=True, capture_output=True)
    assert (topdir / "modules" / "hal_opt" / ".git").is_dir()

    (tmp_path / "inactive").mkdir()
    generated = run_zephyr_module(topdir, tmp_path / "inactive")

    assert "ZEPHYR_HAL_OPT_MODULE" not in generated["Kconfig.modules"]
    assert "hal_opt" not in generated["zephyr_modules.txt"]

    # Enabling the group is what turns the clone into a module.
    subprocess.run(
        ["west", "config", "manifest.group-filter", "--", "+optional"],
        cwd=topdir,
        check=True,
        capture_output=True,
    )

    (tmp_path / "active").mkdir()
    generated = run_zephyr_module(topdir, tmp_path / "active")

    assert "config ZEPHYR_HAL_OPT_MODULE\n\tbool\n\tdefault y" in generated["Kconfig.modules"]
    assert '"hal_opt"' in generated["zephyr_modules.txt"]
