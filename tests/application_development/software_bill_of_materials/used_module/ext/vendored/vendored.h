/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Example Vendor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SBOM_VENDORED_H_
#define SBOM_VENDORED_H_

/**
 * @brief Stand-in for an API provided by a vendored third-party library.
 *
 * This directory imitates an upstream project that a Zephyr module carries a
 * local copy of instead of tracking it as its own west manifest project (the
 * way MCUboot bundles TinyCrypt under ext/tinycrypt). The enclosing module
 * declares it under 'bundled-components' in its zephyr/module.yml, so
 * 'west spdx' must report it as a package of its own.
 *
 * @return An arbitrary value.
 */
int sbom_vendored_value(void);

/**
 * @brief A vendored helper whose source file carries no license header.
 *
 * Its license and copyright come from the hosting module's REUSE.toml.
 *
 * @return An arbitrary value.
 */
int sbom_vendored_from_reuse_toml(void);

#endif /* SBOM_VENDORED_H_ */
