/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_ULL_CHAN_INTERNAL_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_ULL_CHAN_INTERNAL_H_

void ull_chan_reset(void);
uint8_t ull_chan_map_get(uint8_t *const chan_map);
void ull_chan_map_set(uint8_t const *const chan_map);

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_ULL_CHAN_INTERNAL_H_ */
