/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_MESHTASTIC_SETTINGS_H_
#define ZEPHYR_SUBSYS_MESHTASTIC_SETTINGS_H_

#include <zephyr/meshtastic/meshtastic.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_MESHTASTIC_SETTINGS)
int meshtastic_settings_init(void);
void meshtastic_settings_schedule_save(void);
int meshtastic_settings_flush(void);
#else
static inline int meshtastic_settings_init(void)
{
	return 0;
}

static inline void meshtastic_settings_schedule_save(void)
{
}

static inline int meshtastic_settings_flush(void)
{
	return 0;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_MESHTASTIC_SETTINGS_H_ */
