/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_HAL_RADIO_VENDOR_HAL_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_HAL_RADIO_VENDOR_HAL_H_

#include "hal/RV32M1/radio/radio.h"

/* The openisa vendor HAL does not have the GPIO support functions
 * required for handling radio front-end modules with PA/LNAs.
 *
 * If these are ever implemented, this file should be updated
 * appropriately.
 */
#undef HAL_RADIO_GPIO_HAVE_PA_PIN
#undef HAL_RADIO_GPIO_HAVE_LNA_PIN

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_HAL_RADIO_VENDOR_HAL_H_ */
