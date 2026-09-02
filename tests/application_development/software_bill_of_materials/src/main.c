/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * Copyright (c) 2026 The Linux Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <answer.h>

int main(void)
{
	/* Reference the "used" module so its code and embedded blobs are linked
	 * into the image.
	 */
	return sbom_used_module_answer() == 42 && sbom_used_module_firmware_size() > 0 ? 0 : 1;
}
