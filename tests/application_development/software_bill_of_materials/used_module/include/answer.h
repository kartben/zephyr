/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SBOM_USED_MODULE_ANSWER_H_
#define SBOM_USED_MODULE_ANSWER_H_

/**
 * @brief Return the answer computed by the "used" SBOM demo module.
 *
 * The application calls this from main() so that the module's compiled code is
 * pulled into the final image and its source appears in the generated SBOM.
 *
 * @return The answer to the ultimate question of life, the universe, and everything.
 */
int sbom_used_module_answer(void);

/**
 * @brief A helper whose source file carries no license/copyright header.
 *
 * src/no_header.c has no SPDX tags of its own; its license and copyright are
 * supplied by the module's REUSE.toml. It exists to prove that 'west spdx'
 * honours REUSE.toml annotations when analyzing a source file.
 *
 * @return An arbitrary value.
 */
int sbom_used_module_from_reuse_toml(void);

/**
 * @brief Total size of the firmware blobs this module embeds.
 *
 * src/firmware.c embeds two of the module's binary blobs: one rendered as a C
 * array by generate_inc_file_for_target(), one pulled in by an .incbin
 * directive naming a compile definition. Neither is on the link line, so this
 * is what proves 'west spdx' finds blobs a build merely embeds.
 *
 * @return The combined size, in bytes, of both embedded blobs.
 */
int sbom_used_module_firmware_size(void);

#endif /* SBOM_USED_MODULE_ANSWER_H_ */
