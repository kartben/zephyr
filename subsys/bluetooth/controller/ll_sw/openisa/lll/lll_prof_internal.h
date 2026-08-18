/*
 * Copyright (c) 2018-2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_LLL_LLL_PROF_INTERNAL_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_LLL_LLL_PROF_INTERNAL_H_

void lll_prof_latency_capture(void);
void lll_prof_radio_end_backup(void);
void lll_prof_cputime_capture(void);
void lll_prof_send(void);

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_LLL_LLL_PROF_INTERNAL_H_ */
