/*
 * Copyright (c) 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the Intel LPSS DMA driver APIs.
 * @ingroup dma_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_INTEL_LPSS_H_
#define ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_INTEL_LPSS_H_

/** Offset of the DMA registers within the MMIO region of an LPSS peripheral. */
#define DMA_INTEL_LPSS_OFFSET		0x800
/** Offset of the register holding the lower 32 bits of the remap address. */
#define DMA_INTEL_LPSS_REMAP_LOW	0x240
/** Offset of the register holding the upper 32 bits of the remap address. */
#define DMA_INTEL_LPSS_REMAP_HI		0x244
/** DMA channel used for transmit transfers. */
#define DMA_INTEL_LPSS_TX_CHAN		0
/** DMA channel used for receive transfers. */
#define DMA_INTEL_LPSS_RX_CHAN		1
/** Bit shift to extract the upper 32 bits of a 64-bit address. */
#define DMA_INTEL_LPSS_ADDR_RIGHT_SHIFT	32

/**
 * @brief Handle interrupts of an Intel LPSS DMA instance.
 *
 * @param dev LPSS DMA device.
 */
void dma_intel_lpss_isr(const struct device *dev);

/**
 * @brief Set up an Intel LPSS DMA instance.
 *
 * The register base address must have been set with
 * dma_intel_lpss_set_base() beforehand.
 *
 * @param dev LPSS DMA device.
 * @return 0 on success, negative error code on failure.
 */
int dma_intel_lpss_setup(const struct device *dev);

/**
 * @brief Set the register base address of an Intel LPSS DMA instance.
 *
 * @param dev LPSS DMA device.
 * @param base Base address of the DMA registers.
 */
void dma_intel_lpss_set_base(const struct device *dev, uintptr_t base);

#endif /* ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_INTEL_LPSS_H_ */
