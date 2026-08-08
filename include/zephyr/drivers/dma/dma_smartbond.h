/*
 * Copyright (c) 2023 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for Renesas SmartBond vendor specific DMA trigger sources.
 * @ingroup dma_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_SMARTBOND_H_
#define ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_SMARTBOND_H_

/**
 * @brief Vendror-specific DMA peripheral triggering sources.
 *
 * A valid triggering source should be provided when DMA
 * is configured for peripheral to peripheral or memory to peripheral
 * transactions.
 */
enum dma_smartbond_trig_mux {
	DMA_SMARTBOND_TRIG_MUX_SPI   = 0x0, /**< Triggered by the SPI peripheral */
	DMA_SMARTBOND_TRIG_MUX_SPI2  = 0x1, /**< Triggered by the SPI2 peripheral */
	DMA_SMARTBOND_TRIG_MUX_UART  = 0x2, /**< Triggered by the UART peripheral */
	DMA_SMARTBOND_TRIG_MUX_UART2 = 0x3, /**< Triggered by the UART2 peripheral */
	DMA_SMARTBOND_TRIG_MUX_I2C   = 0x4, /**< Triggered by the I2C peripheral */
	DMA_SMARTBOND_TRIG_MUX_I2C2  = 0x5, /**< Triggered by the I2C2 peripheral */
	DMA_SMARTBOND_TRIG_MUX_USB   = 0x6, /**< Triggered by the USB peripheral */
	DMA_SMARTBOND_TRIG_MUX_UART3 = 0x7, /**< Triggered by the UART3 peripheral */
	DMA_SMARTBOND_TRIG_MUX_PCM   = 0x8, /**< Triggered by the PCM audio interface */
	DMA_SMARTBOND_TRIG_MUX_SRC   = 0x9, /**< Triggered by the audio sample rate converter */
	DMA_SMARTBOND_TRIG_MUX_GPADC = 0xC, /**< Triggered by the general purpose ADC */
	DMA_SMARTBOND_TRIG_MUX_SDADC = 0xD, /**< Triggered by the sigma delta ADC */
	DMA_SMARTBOND_TRIG_MUX_NONE  = 0xF  /**< No peripheral triggering source */
};

#endif /* ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_SMARTBOND_H_ */
