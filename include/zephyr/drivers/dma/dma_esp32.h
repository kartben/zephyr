/*
 * Copyright (c) 2022 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for Espressif ESP32 DMA driver GDMA trigger sources and Devicetree helpers.
 * @ingroup dma_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_ESP32_H_
#define ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_ESP32_H_

/** @brief Peripherals which can trigger GDMA transfers. */
enum gdma_trigger_peripheral {
	ESP_GDMA_TRIG_PERIPH_M2M = -1,       /**< Memory-to-memory transfer, no peripheral. */
	ESP_GDMA_TRIG_PERIPH_SPI2 = 0,       /**< SPI2 peripheral. */
	ESP_GDMA_TRIG_PERIPH_SPI3 = 1,       /**< SPI3 peripheral. */
	ESP_GDMA_TRIG_PERIPH_UHCI0 = 2,      /**< UHCI0 (UART DMA) peripheral. */
	ESP_GDMA_TRIG_PERIPH_I2S0 = 3,       /**< I2S0 peripheral. */
	ESP_GDMA_TRIG_PERIPH_I2S1 = 4,       /**< I2S1 peripheral. */
	ESP_GDMA_TRIG_PERIPH_LCD0 = 5,       /**< LCD0 peripheral. */
	ESP_GDMA_TRIG_PERIPH_CAM0 = 5,       /**< CAM0 peripheral. */
	ESP_GDMA_TRIG_PERIPH_AES = 6,        /**< AES peripheral. */
	ESP_GDMA_TRIG_PERIPH_SHA = 7,        /**< SHA peripheral. */
	ESP_GDMA_TRIG_PERIPH_ADC0 = 8,       /**< ADC0 peripheral. */
	ESP_GDMA_TRIG_PERIPH_DAC0 = 8,       /**< DAC0 peripheral. */
	ESP_GDMA_TRIG_PERIPH_RMT = 9,        /**< RMT peripheral. */
	ESP_GDMA_TRIG_PERIPH_INVALID = 0x3F, /**< No trigger peripheral selected. */
};

/**
 * @brief Get the DMA controller device for a named element of a DT instance dmas property.
 *
 * @param n Devicetree instance number.
 * @param name Lowercase-and-underscores name of the dmas element as given in dma-names.
 * @return Pointer to the DMA controller device, or NULL if the instance has no dmas property.
 */
#define ESP32_DT_INST_DMA_CTLR(n, name)			\
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, dmas),		\
		    (DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, name))),	\
		    (NULL))

/**
 * @brief Get a cell value from a named element of a DT instance dmas property.
 *
 * @param n Devicetree instance number.
 * @param name Lowercase-and-underscores name of the dmas element as given in dma-names.
 * @param cell Lowercase-and-underscores cell name.
 * @return Cell value, or 0xff if the instance has no dmas property.
 */
#define ESP32_DT_INST_DMA_CELL(n, name, cell)		\
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, dmas),		\
		    (DT_INST_DMAS_CELL_BY_NAME(n, name, cell)),	\
		    (0xff))


#endif /* ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_ESP32_H_ */
