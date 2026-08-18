/*
 * Copyright (c) 2016 Nordic Semiconductor ASA
 * Copyright (c) 2016 Vinayak Kariappa Chettimada
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_HAL_CCM_H_
#define ZEPHYR_BT_CONTROLLER_HAL_CCM_H_

struct ccm {
	uint8_t  key[16];
	uint64_t counter;
	uint8_t  direction:1;
	uint8_t  resv1:7;
	uint8_t  iv[8];
} __packed;

#endif /* ZEPHYR_BT_CONTROLLER_HAL_CCM_H_ */
