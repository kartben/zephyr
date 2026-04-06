# Copyright (c) 2026 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

from zspdx.cmakequery import getCMakeQueryBuildDirs


def write_domains_yaml(build_dir, app_dir, mcuboot_dir):
    (build_dir / "domains.yaml").write_text(
        "\n".join(
            [
                "default: app",
                f"build_dir: {build_dir}",
                "domains:",
                "  - name: app",
                f"    build_dir: {app_dir}",
                "  - name: mcuboot",
                f"    build_dir: {mcuboot_dir}",
                "flash_order:",
                "  - app",
                "  - mcuboot",
                "",
            ]
        ),
        encoding="utf-8",
    )


def test_get_cmake_query_build_dirs_for_plain_build(tmp_path):
    build_dir = tmp_path / "build"

    assert getCMakeQueryBuildDirs(build_dir) == [str(build_dir.resolve())]


def test_get_cmake_query_build_dirs_for_sysbuild_root(tmp_path):
    build_dir = tmp_path / "build"
    app_dir = build_dir / "app"
    mcuboot_dir = build_dir / "mcuboot"
    build_dir.mkdir()
    write_domains_yaml(build_dir, app_dir, mcuboot_dir)

    assert getCMakeQueryBuildDirs(build_dir) == [
        str(build_dir.resolve()),
        str(app_dir.resolve()),
        str(mcuboot_dir.resolve()),
    ]


def test_get_cmake_query_build_dirs_for_sysbuild_image(tmp_path):
    build_dir = tmp_path / "build"
    app_dir = build_dir / "app"
    mcuboot_dir = build_dir / "mcuboot"
    build_dir.mkdir()
    write_domains_yaml(build_dir, app_dir, mcuboot_dir)

    assert getCMakeQueryBuildDirs(app_dir) == [
        str(app_dir.resolve()),
        str(build_dir.resolve()),
    ]
