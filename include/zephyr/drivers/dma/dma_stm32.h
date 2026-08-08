/*
 * Copyright (c) 2021 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for STM32 DMA driver Devicetree helper macros.
 * @ingroup dma_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_STM32_H_
#define ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_STM32_H_

#include <zephyr/devicetree.h>
#include <zephyr/devicetree/dma.h>

/**
 * @brief linked_channel value informing the Zephyr DMA driver that the
 * DMA channel is handled by the STM32 HAL
 */
#define STM32_DMA_HAL_OVERRIDE      0x7F

/**
 * @brief Id of the first DMA channel or stream in the devicetree: 0 or 1.
 *
 * Subtract this offset from a devicetree DMA channel id to get the 0-based
 * channel index in the register map.
 */
#if defined(CONFIG_DMA_STM32U5)
/* from DTS the dma stream id is in range 0..N-1 */
#define STM32_DMA_STREAM_OFFSET 0
#elif !defined(CONFIG_DMA_STM32_V1)
/* from DTS the dma stream id is in range 1..N */
/* so decrease to set range from 0 from now on */
#define STM32_DMA_STREAM_OFFSET 1
#elif defined(CONFIG_DMA_STM32_V1) && defined(CONFIG_DMAMUX_STM32)
/* typically on the stm32H7 series, DMA V1 with mux */
#define STM32_DMA_STREAM_OFFSET 1
#else
/* from DTS the dma stream id is in range 0..N-1 */
#define STM32_DMA_STREAM_OFFSET 0
#endif /* ! CONFIG_DMA_STM32_V1 */

/* macro for dma slot (only for dma-v1 or dma-v2 types) */
#if DT_HAS_COMPAT_STATUS_OKAY(st_stm32_dma_v2bis)
#define STM32_DMA_SLOT(id, dir, slot) 0
#define STM32_DMA_SLOT_BY_IDX(id, idx, slot) 0
#else
/**
 * @brief Get the DMA slot (request line) of a named DMA channel of a device.
 *
 * Evaluates to 0 for DMA variants without a slot cell (dma-v2bis).
 *
 * @param id Devicetree instance number of the device requesting DMA
 * @param dir Name of the element in the "dma-names" property, e.g. tx or rx
 * @param slot Name of the devicetree cell holding the slot value, i.e. slot
 * @return Slot cell value of the selected "dmas" element
 */
#define STM32_DMA_SLOT(id, dir, slot) DT_INST_DMAS_CELL_BY_NAME(id, dir, slot)
/**
 * @brief Get the DMA slot (request line) of a DMA channel of a device by index.
 *
 * Evaluates to 0 for DMA variants without a slot cell (dma-v2bis).
 *
 * @param id Devicetree instance number of the device requesting DMA
 * @param idx Index of the element in the "dmas" property
 * @param slot Name of the devicetree cell holding the slot value, i.e. slot
 * @return Slot cell value of the selected "dmas" element
 */
#define STM32_DMA_SLOT_BY_IDX(id, idx, slot) DT_INST_DMAS_CELL_BY_IDX(id, idx, slot)
#endif

/**
 * @brief Get the features cell of a named DMA channel of a device.
 *
 * Evaluates to 0 for DMA variants without a features cell.
 *
 * @param id Devicetree instance number of the device requesting DMA
 * @param dir Name of the element in the "dma-names" property, e.g. tx or rx
 * @return Features cell value of the selected "dmas" element
 */
#if DT_HAS_COMPAT_STATUS_OKAY(st_stm32_dma_v2) || \
	DT_HAS_COMPAT_STATUS_OKAY(st_stm32_dma_v2bis) || \
	DT_HAS_COMPAT_STATUS_OKAY(st_stm32_dmamux)
#define STM32_DMA_FEATURES(id, dir) 0
#else
#define STM32_DMA_FEATURES(id, dir)						\
		DT_INST_DMAS_CELL_BY_NAME(id, dir, features)
#endif

/**
 * @brief Get the node identifier of the DMA controller of a named DMA channel.
 *
 * @param id Devicetree instance number of the device requesting DMA
 * @param dir Name of the element in the "dma-names" property, e.g. tx or rx
 * @return Node identifier of the DMA controller of the selected "dmas" element
 */
#define STM32_DMA_CTLR(id, dir)						\
		DT_INST_DMAS_CTLR_BY_NAME(id, dir)
/**
 * @brief Get the channel-config cell of a named DMA channel of a device.
 *
 * @param id Devicetree instance number of the device requesting DMA
 * @param dir Name of the element in the "dma-names" property, e.g. tx or rx
 * @return channel-config cell value of the selected "dmas" element
 */
#define STM32_DMA_CHANNEL_CONFIG(id, dir)					\
		DT_INST_DMAS_CELL_BY_NAME(id, dir, channel_config)
/**
 * @brief Get the channel-config cell of a DMA channel of a device by index.
 *
 * @param id Devicetree instance number of the device requesting DMA
 * @param idx Index of the element in the "dmas" property
 * @return channel-config cell value of the selected "dmas" element
 */
#define STM32_DMA_CHANNEL_CONFIG_BY_IDX(id, idx)				\
		DT_INST_DMAS_CELL_BY_IDX(id, idx, channel_config)

/* macros for channel-config */
/**
 * @brief Get the circular mode flag from a channel-config cell value.
 *
 * @param config channel-config cell value from the "dmas" property
 * @return 1 if circular (cyclic) buffer mode is enabled, 0 otherwise
 */
#define STM32_DMA_CONFIG_CYCLIC(config)                 ((config >> 5) & 0x1)
/**
 * @brief Get the transfer direction from a channel-config cell value.
 *
 * @param config channel-config cell value from the "dmas" property
 * @return 0 for memory to memory, 1 for memory to peripheral, 2 for
 *         peripheral to memory
 */
#define STM32_DMA_CONFIG_DIRECTION(config)		((config >> 6) & 0x3)
/**
 * @brief Get the peripheral address increment flag from a channel-config cell value.
 *
 * @param config channel-config cell value from the "dmas" property
 * @return 1 if the peripheral address is incremented after each data transfer,
 *         0 otherwise
 */
#define STM32_DMA_CONFIG_PERIPHERAL_ADDR_INC(config)	((config >> 9) & 0x1)
/**
 * @brief Get the memory address increment flag from a channel-config cell value.
 *
 * @param config channel-config cell value from the "dmas" property
 * @return 1 if the memory address is incremented after each data transfer,
 *         0 otherwise
 */
#define STM32_DMA_CONFIG_MEMORY_ADDR_INC(config)	((config >> 10) & 0x1)
/**
 * @brief Get the peripheral data size from a channel-config cell value.
 *
 * @param config channel-config cell value from the "dmas" property
 * @return Peripheral data size in bytes: 1, 2 or 4
 */
#define STM32_DMA_CONFIG_PERIPHERAL_DATA_SIZE(config)	\
						(1 << ((config >> 11) & 0x3))
/**
 * @brief Get the memory data size from a channel-config cell value.
 *
 * @param config channel-config cell value from the "dmas" property
 * @return Memory data size in bytes: 1, 2 or 4
 */
#define STM32_DMA_CONFIG_MEMORY_DATA_SIZE(config)	\
						(1 << ((config >> 13) & 0x3))
/**
 * @brief Get the peripheral increment offset flag from a channel-config cell value.
 *
 * @param config channel-config cell value from the "dmas" property
 * @return 1 for a fixed 4-byte increment offset, 0 for an offset linked to
 *         the peripheral data size
 */
#define STM32_DMA_CONFIG_PERIPHERAL_INC_FIXED(config)	((config >> 15) & 0x1)
/**
 * @brief Get the channel priority from a channel-config cell value.
 *
 * @param config channel-config cell value from the "dmas" property
 * @return Priority level, from 0 (low) to 3 (very high)
 */
#define STM32_DMA_CONFIG_PRIORITY(config)		((config >> 16) & 0x3)

/**
 * @brief Get the FIFO threshold from a features cell value.
 *
 * Evaluates to 0 for DMA variants other than dma-v1, which have no
 * configurable FIFO.
 *
 * @param features features cell value from the "dmas" property
 * @return FIFO threshold configuration
 */
#if DT_HAS_COMPAT_STATUS_OKAY(st_stm32_dma_v1)
#define STM32_DMA_FEATURES_FIFO_THRESHOLD(features)	(features & 0x3)
#else
#define STM32_DMA_FEATURES_FIFO_THRESHOLD(features)	0
#endif

/**
 * @brief Get the HAL channel instance of a DMA controller channel.
 *
 * Resolves to the STM32Cube LL helper macro of the series in use, taking the
 * DMA controller register address and the channel index as arguments.
 */
#if defined(CONFIG_SOC_SERIES_STM32H5X) || defined(CONFIG_SOC_SERIES_STM32H7RSX) ||                \
	defined(CONFIG_SOC_SERIES_STM32MP2X) || defined(CONFIG_SOC_SERIES_STM32N6X) ||             \
	defined(CONFIG_SOC_SERIES_STM32U3X) || defined(CONFIG_SOC_SERIES_STM32U5X) ||              \
	defined(CONFIG_SOC_SERIES_STM32WBAX)
#define STM32_DMA_GET_CHANNEL_INSTANCE LL_DMA_GET_CHANNEL_INSTANCE
#else
#define STM32_DMA_GET_CHANNEL_INSTANCE __LL_DMA_GET_CHANNEL_INSTANCE
#endif

/**
 * @brief Get the HAL stream or channel instance of a DMA controller channel.
 *
 * @param reg DMA controller register address
 * @param channel DMA channel id as used in the devicetree
 * @return Address of the HAL stream (dma-v1) or channel instance
 */
#if DT_HAS_COMPAT_STATUS_OKAY(st_stm32_dma_v1)
#define STM32_DMA_GET_INSTANCE(reg, channel)				\
		__LL_DMA_GET_STREAM_INSTANCE((reg), (channel) - STM32_DMA_STREAM_OFFSET);
#else
#define STM32_DMA_GET_INSTANCE(reg, channel)				\
		STM32_DMA_GET_CHANNEL_INSTANCE((reg), (channel) - STM32_DMA_STREAM_OFFSET);
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_STM32_H_ */
