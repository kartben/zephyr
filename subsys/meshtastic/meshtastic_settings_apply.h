/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MESHTASTIC_SETTINGS_APPLY_H_
#define ZEPHYR_SUBSYS_MESHTASTIC_SETTINGS_APPLY_H_

#include <zephyr/sys/iterable_sections.h>

#ifdef __cplusplus
extern "C" {
#endif

struct meshtastic_settings_apply {
	const char *name;
	int (*apply)(void);
};

#define MESHTASTIC_SETTINGS_APPLY_DEFINE(_name, _apply)                                           \
	static const STRUCT_SECTION_ITERABLE(meshtastic_settings_apply,                           \
					     _meshtastic_settings_apply_##_name) = {                  \
		.name = STRINGIFY(_name), .apply = (_apply),                                         \
	}

int meshtastic_settings_apply_all(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MESHTASTIC_SETTINGS_APPLY_H_ */
