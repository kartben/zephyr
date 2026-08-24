/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <answer.h>
#include <vendored.h>

int sbom_used_module_answer(void)
{
	/* Pull the vendored library's code into the build as well. */
	return 29 + sbom_vendored_value();
}
