# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(src/answer.c)
# no_header.c has no SPDX tags of its own; its license/copyright come from
# REUSE.toml. Compiling it lets the SBOM tests verify REUSE.toml support.
zephyr_library_sources(src/no_header.c)
# ext/vendored imitates a third-party project vendored inside the module rather
# than tracked as its own west project. The module declares it under
# 'bundled-components' in zephyr/module.yml, so the SBOM tests can verify that
# its source is attributed to a package of its own instead of to the module.
zephyr_library_sources(ext/vendored/vendored.c)
zephyr_include_directories(include ext/vendored)
