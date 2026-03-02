/**
 * @file
 * @brief TI BQ274xx fuel gauge Devicetree constants
 */

/*
 * Copyright (c) 2023 The Zephyr Contributors.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Relevant documents:
 * - BQ27421
 *   Datasheet: https://www.ti.com/lit/gpn/bq27421-g1
 *   Technical reference manual: https://www.ti.com/lit/pdf/sluuac5
 * - BQ27427
 *   Datasheet: https://www.ti.com/lit/gpn/bq27427
 *   Technical reference manual: https://www.ti.com/lit/pdf/sluucd5
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_SENSOR_BQ274XX_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_SENSOR_BQ274XX_H_

/**
 * @defgroup bq274xx_dt_api TI BQ274xx Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @addtogroup bq27427_chem_id BQ27427 chemistry IDs
 * @{
 */
#define BQ27427_CHEM_ID_A 0x3230 /**< 4.35 V cell chemistry */
#define BQ27427_CHEM_ID_B 0x1202 /**< 4.2 V cell chemistry */
#define BQ27427_CHEM_ID_C 0x3142 /**< 4.4 V cell chemistry */
/** @} */

/**
 * @addtogroup bq27421_chem_id BQ27421 chemistry IDs
 * @{
 */
#define BQ27421_G1A_CHEM_ID 0x0128 /**< 4.2 V cell chemistry */
#define BQ27421_G1B_CHEM_ID 0x0312 /**< 4.3 to 4.35 V cell chemistry */
#define BQ27421_G1D_CHEM_ID 0x3142 /**< 4.3 to 4.4 V cell chemistry */
/** @} */


/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_SENSOR_BQ274XX_H_ */
