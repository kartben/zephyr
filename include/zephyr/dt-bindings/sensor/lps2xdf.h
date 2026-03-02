/**
 * @file
 * @brief ST LPS2xDF pressure sensor family Devicetree constants
 */

/*
 * Copyright (c) 2023 STMicroelectronics
 * Copyright (c) 2023 PHYTEC Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ST_LPS2xDF_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ST_LPS2xDF_H_

/**
 * @defgroup lps2xdf_dt_api ST LPS2xDF pressure sensor Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup lps2xdf_odr Output data rates
 * @{
 */
#define LPS2xDF_DT_ODR_POWER_DOWN		0 /**< Power-down */
#define LPS2xDF_DT_ODR_1HZ			1 /**< 1 Hz */
#define LPS2xDF_DT_ODR_4HZ			2 /**< 4 Hz */
#define LPS2xDF_DT_ODR_10HZ			3 /**< 10 Hz */
#define LPS2xDF_DT_ODR_25HZ			4 /**< 25 Hz */
#define LPS2xDF_DT_ODR_50HZ			5 /**< 50 Hz */
#define LPS2xDF_DT_ODR_75HZ			6 /**< 75 Hz */
#define LPS2xDF_DT_ODR_100HZ			7 /**< 100 Hz */
#define LPS2xDF_DT_ODR_200HZ			8 /**< 200 Hz */
/** @} */

/**
 * @defgroup lps2xdf_lp_filter Low-pass filter configuration
 * @{
 */
#define LPS2xDF_DT_LP_FILTER_OFF		0 /**< Low-pass filter disabled */
#define LPS2xDF_DT_LP_FILTER_ODR_4		1 /**< Bandwidth = ODR/4 */
#define LPS2xDF_DT_LP_FILTER_ODR_9		3 /**< Bandwidth = ODR/9 */
/** @} */

/**
 * @defgroup lps2xdf_avg Averaging filter (number of samples)
 * @{
 */
#define LPS2xDF_DT_AVG_4_SAMPLES		0 /**< 4 samples */
#define LPS2xDF_DT_AVG_8_SAMPLES		1 /**< 8 samples */
#define LPS2xDF_DT_AVG_16_SAMPLES		2 /**< 16 samples */
#define LPS2xDF_DT_AVG_32_SAMPLES		3 /**< 32 samples */
#define LPS2xDF_DT_AVG_64_SAMPLES		4 /**< 64 samples */
#define LPS2xDF_DT_AVG_128_SAMPLES		5 /**< 128 samples */
#define LPS2xDF_DT_AVG_256_SAMPLES		6 /**< 256 samples */
#define LPS2xDF_DT_AVG_512_SAMPLES		7 /**< 512 samples */
/** @} */

/**
 * @defgroup lps28dfw_fs LPS28DFW full-scale pressure range
 * @{
 */
#define LPS28DFW_DT_FS_MODE_1_1260		0 /**< 0 to 1260 hPa */
#define LPS28DFW_DT_FS_MODE_2_4060		1 /**< 0 to 4060 hPa */
/** @} */

/**
 * @defgroup ilps22qs_fs ILPS22QS full-scale pressure range
 * @{
 */
#define ILPS22QS_DT_FS_MODE_1_1260		0 /**< 0 to 1260 hPa */
#define ILPS22QS_DT_FS_MODE_2_4060		1 /**< 0 to 4060 hPa */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ST_LPS22DF_H_ */
