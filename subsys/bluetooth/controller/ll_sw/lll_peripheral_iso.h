/*
 * Copyright (c) 2021 Demant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_LLL_PERIPHERAL_ISO_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_LLL_PERIPHERAL_ISO_H_

int lll_peripheral_iso_init(void);
int lll_peripheral_iso_reset(void);
void lll_peripheral_iso_prepare(void *param);

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_LLL_PERIPHERAL_ISO_H_ */
