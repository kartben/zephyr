/*
 * Copyright (c) 2018-2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_LLL_CENTRAL_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_LLL_CENTRAL_H_

int lll_central_init(void);
int lll_central_reset(void);
void lll_central_prepare(void *param);

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_LLL_CENTRAL_H_ */
