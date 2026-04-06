# Copyright (c) 2026 The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for SPDX build directory resolution with sysbuild."""

import textwrap

from domains import zephyr_cmake_build_dir_for_spdx


def test_single_image_no_domains_yaml(tmp_path):
    b = tmp_path / "build"
    b.mkdir()
    image, is_top = zephyr_cmake_build_dir_for_spdx(str(b))
    assert image == str(b.resolve())
    assert is_top is False


def test_sysbuild_top_remaps_to_default_image(tmp_path):
    top = tmp_path / "build"
    top.mkdir()
    app = top / "my_app"
    app.mkdir()
    (top / "domains.yaml").write_text(
        textwrap.dedent(
            f"""\
            default: my_app
            build_dir: {top}
            domains:
              - name: my_app
                build_dir: {app}
              - name: mcuboot
                build_dir: {top / "mcuboot"}
            flash_order:
              - my_app
            """
        ),
        encoding="utf-8",
    )

    image, is_top = zephyr_cmake_build_dir_for_spdx(str(top))
    assert image == str(app.resolve())
    assert is_top is True


def test_explicit_image_dir_not_remapped(tmp_path):
    top = tmp_path / "build"
    top.mkdir()
    app = top / "my_app"
    app.mkdir()
    (top / "domains.yaml").write_text(
        textwrap.dedent(
            f"""\
            default: my_app
            build_dir: {top}
            domains:
              - name: my_app
                build_dir: {app}
            flash_order:
              - my_app
            """
        ),
        encoding="utf-8",
    )

    image, is_top = zephyr_cmake_build_dir_for_spdx(str(app))
    assert image == str(app.resolve())
    assert is_top is False
