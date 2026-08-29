# Copyright (c) 2026 Zephyr Project members and individual contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Feasibility gate for fetching a single inactive west project on demand.

Zephyr is moving towards a model where a build activates only the external
modules it actually depends on. A natural follow-up question is whether the
build system could also *materialize* a required module that is not present in
the workspace yet, by asking west for that one project.

Answering that question is a prerequisite for any such implementation, because
the answer must not be worked around: rewriting ``.west/config``, mutating
``manifest.project-filter``, calling private west APIs or shelling out to git
are all off limits. This file therefore pins down what stock west does today,
so the answer stays true (or fails loudly when a west upgrade changes it).

Outcome, as of west 1.5.0:

* ``west update <project>`` **can** materialize one inactive project, but only
  when that project is defined in the workspace's own manifest repository. It
  works no matter why the project is inactive, it fetches nothing else, and it
  leaves ``.west/config`` untouched. Downstream ``url``/``revision``/``path``
  overrides are honored.
* ``west update <project>`` **refuses** when the project's definition came from
  a manifest ``import``, which is precisely the topology every downstream
  distribution uses to consume Zephyr's own manifest. There is no supported way
  to fetch just that project.
* west also offers no way to tell "the user deliberately excluded this project"
  apart from "this project's group is not enabled": ``Manifest.is_active()``
  answers False for both, and a selective fetch happily overrides an explicit
  ``manifest.project-filter`` exclusion.

So the primitive exists for a direct upstream workspace and is missing for
imported manifests. Implementing automatic fetching therefore requires a new
west capability first, and the module activation work in Zephyr stands on its
own until then.
"""

import shutil

import pytest
from conftest import ManifestLab, is_cloned, west, west_config

pytest.importorskip("west", reason="west is not installed")
pytestmark = pytest.mark.skipif(shutil.which("west") is None, reason="west CLI not on PATH")


@pytest.fixture(scope="module")
def upstream(lab: ManifestLab) -> ManifestLab:
    """A Zephyr-like upstream manifest with active, group-inactive and imported projects."""
    for name in ("cmsis", "hal_tdk", "hal_opt", "sdk_only", "vendor_sdk"):
        lab.add_repo(name)
    lab.fork_repo("hal_tdk_fork", "hal_tdk")

    # vendor_sdk is imported by the upstream manifest and is the only definition
    # site of the sdk_only project.
    vendor_sdk = lab.module_files("vendor_sdk")
    vendor_sdk["west.yml"] = f"""\
        manifest:
          projects:
            - name: sdk_only
              url: {lab.url('sdk_only')}
              revision: {lab.sha('sdk_only')}
              path: modules/sdk_only
        """
    lab.add_repo("vendor_sdk", vendor_sdk)

    lab.add_repo(
        "zephyr",
        {
            "west.yml": f"""\
        manifest:
          group-filter: [-optional]
          projects:
            - name: cmsis
              url: {lab.url('cmsis')}
              revision: {lab.sha('cmsis')}
              path: modules/cmsis
            - name: hal_tdk
              url: {lab.url('hal_tdk')}
              revision: {lab.sha('hal_tdk')}
              path: modules/hal/tdk
              groups: [optional]
            - name: hal_opt
              url: {lab.url('hal_opt')}
              revision: {lab.sha('hal_opt')}
              path: modules/hal/opt
              groups: [optional]
            - name: vendor_sdk
              url: {lab.url('vendor_sdk')}
              revision: {lab.sha('vendor_sdk')}
              path: modules/vendor_sdk
              import: true
        """
        },
    )

    # A downstream distribution: imports the Zephyr manifest and overrides one
    # of its projects, the way real Zephyr-based products do.
    lab.add_repo(
        "downstream",
        {
            "west.yml": f"""\
        manifest:
          projects:
            - name: zephyr
              url: {lab.url('zephyr')}
              revision: {lab.sha('zephyr')}
              path: zephyr
              import: true
            - name: hal_tdk
              url: {lab.url('hal_tdk_fork')}
              revision: {lab.sha('hal_tdk_fork')}
              path: modules/hal/tdk_downstream
        """
        },
    )

    return lab


@pytest.fixture(scope="module")
def upstream_workspace(upstream: ManifestLab):
    """An updated workspace whose manifest repository is the Zephyr-like manifest."""
    topdir = upstream.workspace("upstream", "zephyr")
    west(["update"], topdir)
    return topdir


@pytest.fixture(scope="module")
def downstream_workspace(upstream: ManifestLab):
    """An updated workspace whose manifest repository imports the Zephyr manifest."""
    topdir = upstream.workspace("downstream", "downstream", manifest_path="downstream")
    west(["update"], topdir)
    return topdir


def test_manifest_repository_project_can_be_fetched_while_inactive(upstream_workspace):
    """An inactive project defined in the manifest repository can be fetched by name."""
    topdir = upstream_workspace
    shutil.rmtree(topdir / "modules" / "hal" / "tdk", ignore_errors=True)
    config = west_config(topdir)

    assert "hal_tdk" in west(["list", "-i", "-f", "{name}"], topdir).stdout, (
        "hal_tdk is expected to be inactive in this workspace"
    )

    result = west(["update", "hal_tdk"], topdir, check=False)

    assert result.returncode == 0, result.stderr
    assert is_cloned(topdir, "modules/hal/tdk")
    # Nothing else got dragged in, and no workspace state was rewritten.
    assert not is_cloned(topdir, "modules/hal/opt")
    assert west_config(topdir) == config


def test_project_disabled_by_the_user_is_fetched_anyway(upstream: ManifestLab):
    """A selective fetch overrides an explicit ``manifest.project-filter`` exclusion.

    west has no public API that distinguishes a project the user deliberately
    excluded from one that is merely in a disabled group, so a build system
    cannot honor that intent on its own either.
    """
    topdir = upstream.workspace("user-disabled", "zephyr")
    west(["config", "manifest.project-filter", "--", "-cmsis"], topdir)
    west(["update"], topdir)

    assert not is_cloned(topdir, "modules/cmsis"), "excluded project should not be updated"

    result = west(["update", "cmsis"], topdir, check=False)

    assert result.returncode == 0, result.stderr
    assert is_cloned(topdir, "modules/cmsis"), (
        "west silently fetched a project the user excluded on purpose"
    )


def test_imported_project_cannot_be_fetched_selectively(downstream_workspace):
    """The blocker: projects resolved through a manifest import cannot be named."""
    result = west(["update", "cmsis"], downstream_workspace, check=False)

    assert result.returncode != 0
    assert "resolved via project imports" in result.stderr
    assert "Only plain \"west update\" can currently update them" in result.stderr


def test_project_import_defined_project_cannot_be_fetched_selectively(upstream_workspace):
    """The same blocker applies to projects contributed by a project-level import."""
    result = west(["update", "sdk_only"], upstream_workspace, check=False)

    assert result.returncode != 0
    assert "resolved via project imports" in result.stderr


def test_downstream_overrides_survive_a_selective_fetch(
    upstream: ManifestLab, downstream_workspace
):
    """A project overridden by the manifest repository is fetched as overridden."""
    topdir = downstream_workspace
    shutil.rmtree(topdir / "modules" / "hal" / "tdk_downstream", ignore_errors=True)
    config = west_config(topdir)

    result = west(["update", "hal_tdk"], topdir, check=False)

    assert result.returncode == 0, result.stderr
    assert is_cloned(topdir, "modules/hal/tdk_downstream"), "downstream path was not used"
    assert not is_cloned(topdir, "modules/hal/tdk"), "upstream path was used instead"

    origin = west(["forall", "-c", "git remote get-url origin", "hal_tdk"], topdir).stdout
    assert str(upstream.url("hal_tdk_fork")) in origin, "downstream URL was not used"
    assert west_config(topdir) == config


def test_group_filter_of_an_imported_manifest_is_honored(downstream_workspace):
    """Marking a project optional upstream keeps it inactive in importing workspaces.

    This is what makes a progressive manifest migration meaningful: downstream
    distributions inherit the upstream group filter instead of having to repeat
    it.
    """
    inactive = west(["list", "-i", "-f", "{name}"], downstream_workspace).stdout.split()

    assert "hal_opt" in inactive


def test_groups_cannot_be_combined_with_an_import(lab: ManifestLab, tmp_path_factory):
    """A project carrying manifest imports can never be placed in an optional group.

    Manifest-level metadata providers therefore cannot be made inactive by
    default through groups, which is why they are treated as bootstrap modules
    by the module resolver.
    """
    isolated = ManifestLab(tmp_path_factory.mktemp("groups-with-import"))
    isolated.add_repo("imported")
    isolated.add_repo(
        "manifest-repo",
        {
            "west.yml": f"""\
        manifest:
          group-filter: [-optional]
          projects:
            - name: imported
              url: {isolated.url('imported')}
              revision: {isolated.sha('imported')}
              path: modules/imported
              groups: [optional]
              import: true
        """
        },
    )
    topdir = isolated.workspace("groups-with-import", "manifest-repo", manifest_path="mr")

    result = west(["list"], topdir, check=False)

    assert result.returncode != 0
    assert "\"groups\" cannot be combined with \"import\"" in result.stderr
