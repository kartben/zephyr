/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <answer.h>

/* Rendered from zephyr/blobs/sbom_blob_firmware.bin by generate_inc_file_for_target() */
static const unsigned char sbom_blob_firmware[] = {
#include <sbom_blob_firmware.bin.inc>
};

/* SBOM_BLOB_INCBIN_PATH is a compile definition naming zephyr/blobs/sbom_blob_incbin.bin */
extern const unsigned char sbom_blob_incbin_start[];
extern const unsigned char sbom_blob_incbin_end[];

__asm__(".section .rodata\n"
	".global sbom_blob_incbin_start\n"
	"sbom_blob_incbin_start:\n"
	".incbin \"" SBOM_BLOB_INCBIN_PATH "\"\n"
	".global sbom_blob_incbin_end\n"
	"sbom_blob_incbin_end:\n"
	".previous\n");

int sbom_used_module_firmware_size(void)
{
	return (int)sizeof(sbom_blob_firmware) +
	       (int)(sbom_blob_incbin_end - sbom_blob_incbin_start);
}
