/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_AUDIO_DMIC_NATIVE_SIM_BOTTOM_H_
#define ZEPHYR_DRIVERS_AUDIO_DMIC_NATIVE_SIM_BOTTOM_H_

#ifdef __cplusplus
extern "C" {
#endif

int ns_dmic_open_file_bottom(const char *pathname);
int ns_dmic_seek_to_end_bottom(int fd);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_AUDIO_DMIC_NATIVE_SIM_BOTTOM_H_ */
