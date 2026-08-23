# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Shared fixtures for the Zephyr module tooling tests.

The helpers here build throwaway west workspaces that are backed by local bare
git repositories, so that manifest topologies (imports, overrides, groups) can
be exercised without any network access.
"""

import shutil
import subprocess
import sys
import textwrap
from pathlib import Path

import pytest

# The tools under test live at the top of scripts/, and in scripts/kconfig.
sys.path.insert(0, str(Path(__file__).parents[2]))
sys.path.insert(0, str(Path(__file__).parents[2] / "kconfig"))

# A minimal, valid Zephyr module: enough for zephyr_module.py to recognize it.
MODULE_FILES = {
    "zephyr/module.yml": "name: {name}\nbuild:\n  cmake: zephyr\n  kconfig: zephyr/Kconfig\n",
    "zephyr/CMakeLists.txt": "",
    "zephyr/Kconfig": "",
}


def write_tree(root: Path, files: dict[str, str]) -> Path:
    """Write ``{relative path: content}`` under ``root``, creating directories."""
    for relative, content in files.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(textwrap.dedent(content))
    return root


def git(args: list[str], cwd: Path) -> str:
    """Run a git command, returning its stdout."""
    return subprocess.run(
        ["git", *args], cwd=cwd, check=True, capture_output=True, text=True
    ).stdout.strip()


class ManifestLab:
    """A set of local git remotes out of which west workspaces can be built.

    Repositories are created with :meth:`add_repo` and referred to by name;
    :meth:`url` and :meth:`sha` provide the values needed to write a manifest
    that points at them.
    """

    def __init__(self, root: Path):
        self.root = root
        self.remotes = root / "remotes"
        self.remotes.mkdir(parents=True, exist_ok=True)
        self._shas: dict[str, str] = {}

    def add_repo(self, name: str, files: dict[str, str] | None = None) -> str:
        """Create (or replace) a bare remote named ``name`` holding ``files``."""
        work = self.root / "work" / name
        if work.exists():
            shutil.rmtree(work)
        for rel, content in (files if files is not None else self.module_files(name)).items():
            path = work / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(textwrap.dedent(content))

        git(["init", "-q", "-b", "main"], work)
        git(["add", "-A"], work)
        git(
            [
                "-c",
                "user.email=ci@zephyrproject.org",
                "-c",
                "user.name=CI",
                "commit",
                "-q",
                "-m",
                "initial commit",
            ],
            work,
        )

        bare = self.remotes / f"{name}.git"
        if bare.exists():
            shutil.rmtree(bare)
        git(["clone", "-q", "--bare", str(work), str(bare)], self.root)

        self._shas[name] = git(["rev-parse", "HEAD"], work)
        return self._shas[name]

    def fork_repo(self, name: str, source: str) -> None:
        """Create a second remote named ``name`` with the history of ``source``."""
        git(
            ["clone", "-q", "--bare", str(self.url(source)), str(self.remotes / f"{name}.git")],
            self.root,
        )
        self._shas[name] = self._shas[source]

    @staticmethod
    def module_files(name: str) -> dict[str, str]:
        return {rel: content.format(name=name) for rel, content in MODULE_FILES.items()}

    def url(self, name: str) -> Path:
        return self.remotes / f"{name}.git"

    def sha(self, name: str) -> str:
        return self._shas[name]

    def workspace(self, name: str, manifest_repo: str, manifest_path: str = "zephyr") -> Path:
        """Clone ``manifest_repo`` and run ``west init -l`` on it."""
        topdir = self.root / "workspaces" / name
        topdir.mkdir(parents=True)
        git(["clone", "-q", str(self.url(manifest_repo)), str(topdir / manifest_path)], self.root)
        west(["init", "-l", str(topdir / manifest_path)], topdir)
        return topdir


def west(args: list[str], topdir: Path, check: bool = True) -> subprocess.CompletedProcess:
    """Run a west command inside ``topdir``."""
    return subprocess.run(["west", *args], cwd=topdir, check=check, capture_output=True, text=True)


def west_config(topdir: Path) -> str:
    """Return the workspace's ``.west/config`` contents."""
    return (topdir / ".west" / "config").read_text()


def is_cloned(topdir: Path, path: str) -> bool:
    """Has the project at workspace-relative ``path`` been materialized?"""
    return (topdir / path / ".git").exists()


@pytest.fixture(scope="module")
def lab(tmp_path_factory) -> ManifestLab:
    """A :class:`ManifestLab` rooted in a temporary directory."""
    return ManifestLab(tmp_path_factory.mktemp("manifest-lab"))
