/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/logging/log.h>

#include "meshtastic_settings_apply.h"

LOG_MODULE_DECLARE(meshtastic, CONFIG_MESHTASTIC_LOG_LEVEL);

int meshtastic_settings_apply_all(void)
{
	int ret;

	STRUCT_SECTION_FOREACH(meshtastic_settings_apply, hook) {
		if (hook->apply == NULL) {
			continue;
		}

		ret = hook->apply();
		if (ret < 0) {
			LOG_WRN("Meshtastic settings apply hook '%s' failed (%d)",
				hook->name, ret);
			return ret;
		}
	}

	return 0;
}
