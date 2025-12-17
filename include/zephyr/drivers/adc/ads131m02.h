/*
 * Copyright (c) 2024 Linumiz
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup adc_ads131m02
 * @brief API for ADS131M02 ADC driver.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ADC_ADS131M02_H_
#define ZEPHYR_INCLUDE_DRIVERS_ADC_ADS131M02_H_

/**
 * @brief ADS131M02 ADC Driver API
 * @defgroup adc_ads131m02 ADS131M02 ADC
 * @ingroup adc_interface
 * @{
 */

#include <zephyr/device.h>

/**
 * @brief ADC operating modes
 *
 * Defines the operating modes available for the ADS131M02 ADC.
 */
enum ads131m02_adc_mode {
	ADS131M02_CONTINUOUS_MODE,  /**< Continuous conversion mode */
	ADS131M02_GLOBAL_CHOP_MODE  /**< Global chop mode */
};

/**
 * @brief ADC power modes
 *
 * Defines the power consumption modes available for the ADS131M02 ADC.
 */
enum ads131m02_adc_power_mode {
	ADS131M02_VLP,  /**< Very Low Power mode */
	ADS131M02_LP,   /**< Low Power mode */
	ADS131M02_HR    /**< High Resolution mode */
};

/**
 * @brief Global chop delay values
 *
 * Defines the delay values for global chop mode.
 */
enum ads131m02_gc_delay {
	ADS131M02_GC_DELAY_2,      /**< Delay of 2 samples */
	ADS131M02_GC_DELAY_4,      /**< Delay of 4 samples */
	ADS131M02_GC_DELAY_8,      /**< Delay of 8 samples */
	ADS131M02_GC_DELAY_16,     /**< Delay of 16 samples */
	ADS131M02_GC_DELAY_32,     /**< Delay of 32 samples */
	ADS131M02_GC_DELAY_64,     /**< Delay of 64 samples */
	ADS131M02_GC_DELAY_128,    /**< Delay of 128 samples */
	ADS131M02_GC_DELAY_256,    /**< Delay of 256 samples */
	ADS131M02_GC_DELAY_512,    /**< Delay of 512 samples */
	ADS131M02_GC_DELAY_1024,   /**< Delay of 1024 samples */
	ADS131M02_GC_DELAY_2048,   /**< Delay of 2048 samples */
	ADS131M02_GC_DELAY_4096,   /**< Delay of 4096 samples */
	ADS131M02_GC_DELAY_8192,   /**< Delay of 8192 samples */
	ADS131M02_GC_DELAY_16384,  /**< Delay of 16384 samples */
	ADS131M02_GC_DELAY_32768,  /**< Delay of 32768 samples */
	ADS131M02_GC_DELAY_65536   /**< Delay of 65536 samples */
};

/**
 * @brief Set ADC operating mode
 *
 * Configures the ADS131M02 ADC operating mode and global chop delay.
 *
 * @param dev      Pointer to the device structure for the driver instance.
 * @param mode     Desired ADC operating mode.
 * @param gc_delay Global chop delay value (only applicable in global chop mode).
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int ads131m02_set_adc_mode(const struct device *dev, enum ads131m02_adc_mode mode,
			   enum ads131m02_gc_delay gc_delay);

/**
 * @brief Set ADC power mode
 *
 * Configures the ADS131M02 ADC power consumption mode.
 *
 * @param dev  Pointer to the device structure for the driver instance.
 * @param mode Desired power mode.
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int ads131m02_set_power_mode(const struct device *dev,
			     enum ads131m02_adc_power_mode mode);

/**
 * @}
 */

#endif
