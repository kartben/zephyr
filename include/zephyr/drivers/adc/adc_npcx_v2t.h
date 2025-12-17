/*
 * Copyright (c) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup adc_npcx_v2t
 * @brief API for NPCX ADC Voltage-to-Temperature (V2T) mode.
 */

#ifndef _ADC_NPCX_V2T_H_
#define _ADC_NPCX_V2T_H_

/**
 * @brief NPCX ADC Voltage-to-Temperature API
 * @defgroup adc_npcx_v2t NPCX ADC V2T
 * @ingroup adc_interface
 * @{
 */

#include <zephyr/device.h>

#ifdef CONFIG_ADC_V2T_NPCX

/**
 * @brief Set ADC V2T channels
 *
 * Configures which ADC channels should operate in Voltage-to-Temperature (V2T) mode.
 *
 * @param dev       Pointer to the ADC device instance.
 * @param channels  Bit-mask indicating the channels to be configured as V2T.
 *
 * @retval 0 on success.
 * @retval Negative error code if configuration fails.
 */
int adc_npcx_v2t_set_channels(const struct device *dev, uint32_t channels);

/**
 * @brief Get ADC V2T configured channels
 *
 * Retrieves the current V2T channel configuration.
 *
 * @param dev  Pointer to the ADC device instance.
 *
 * @return Configured V2T channels as a bit-mask.
 *         Bit value 0 indicates ADC mode, 1 indicates V2T mode.
 */
uint32_t adc_npcx_v2t_get_channels(const struct device *dev);

/**
 * @}
 */

#endif /* CONFIG_ADC_V2T_NPCX */
#endif /*_ADC_NPCX_V2T_H_ */
