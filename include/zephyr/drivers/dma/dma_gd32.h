/*
 * Copyright (c) 2022 TOKITA Hiroshi <tokita.hiroshi@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for GigaDevice GD32 DMA driver configuration helper macros.
 * @ingroup dma_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_GD32_H_
#define ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_GD32_H_

/**
 * @brief Get the transfer direction from a GD32 DMA config cell.
 * @param config Value of the config cell of a dmas property.
 * @return Transfer direction (see dma.h).
 */
#define GD32_DMA_CONFIG_DIRECTION(config)	     ((config >> 6) & 0x3)
/**
 * @brief Get the peripheral address increment flag from a GD32 DMA config cell.
 * @param config Value of the config cell of a dmas property.
 * @return 1 if the peripheral address is incremented between transfers, 0 otherwise.
 */
#define GD32_DMA_CONFIG_PERIPH_ADDR_INC(config)	     ((config >> 9) & 0x1)
/**
 * @brief Get the memory address increment flag from a GD32 DMA config cell.
 * @param config Value of the config cell of a dmas property.
 * @return 1 if the memory address is incremented between transfers, 0 otherwise.
 */
#define GD32_DMA_CONFIG_MEMORY_ADDR_INC(config)	     ((config >> 10) & 0x1)
/**
 * @brief Get the peripheral data width from a GD32 DMA config cell.
 * @param config Value of the config cell of a dmas property.
 * @return Peripheral data width (0: 8 bits, 1: 16 bits, 2: 32 bits).
 */
#define GD32_DMA_CONFIG_PERIPH_WIDTH(config)	     ((config >> 11) & 0x3)
/**
 * @brief Get the memory data width from a GD32 DMA config cell.
 * @param config Value of the config cell of a dmas property.
 * @return Memory data width (0: 8 bits, 1: 16 bits, 2: 32 bits).
 */
#define GD32_DMA_CONFIG_MEMORY_WIDTH(config)	     ((config >> 13) & 0x3)
/**
 * @brief Get the peripheral increment offset size flag from a GD32 DMA config cell.
 * @param config Value of the config cell of a dmas property.
 * @return 1 if the offset size is fixed to 4, 0 if it is linked to the peripheral bus width.
 */
#define GD32_DMA_CONFIG_PERIPHERAL_INC_FIXED(config) ((config >> 15) & 0x1)
/**
 * @brief Get the channel priority from a GD32 DMA config cell.
 * @param config Value of the config cell of a dmas property.
 * @return Channel priority (0: low to 3: very high).
 */
#define GD32_DMA_CONFIG_PRIORITY(config)	     ((config >> 16) & 0x3)

/**
 * @brief Get the FIFO depth used by burst transfers from a GD32 DMA fifo-threshold cell.
 * @param threshold Value of the fifo-threshold cell of a dmas property.
 * @return FIFO threshold setting.
 */
#define GD32_DMA_FEATURES_FIFO_THRESHOLD(threshold) (threshold & 0x3)

#endif /* ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_GD32_H_ */
