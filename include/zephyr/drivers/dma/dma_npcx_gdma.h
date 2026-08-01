/*
 * Copyright (c) 2025 Nuvoton Technology Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/**
 * @file
 * @brief Header file for Nuvoton NPCX GDMA driver channel configuration macros.
 * @ingroup dma_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_NPCX_GDMA_H_
#define ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_NPCX_GDMA_H_

/** Address alignment in bytes required by 16-byte transfer mode. */
#define NPCX_DMA_ADDR_16B_ALIGN 16U

/* Macros for DMA channel-config */
/* config: A 32bit mask specifying the GDMA channel configuration
 *           - bit 0-5:  Reserved. (default: 0x0)
 *           - bit 6-7:  Direction  (see dma.h)
 *                       - 0x0: MEM to MEM
 *                       - 0x1: MEM to PERIPH
 *                       - 0x2: PERIPH to MEM
 *                       - 0x3: reserved for PERIPH to PERIPH
 *           - bit 8-9:  Transfer Width Select. The address width must be aligned
 *                       with the selected data width.
 *                       - 0x0: Byte (8 bits)
 *                       - 0x1: Word (16 bits)
 *                       - 0x2: Double-Word (32 bits)
 *                              This value is required if bit 10 is set to 1.
 *                       - 0x3: Reserved
 *           - bit 10:   16-byte transfer (address must be 16-byte aligned)
 *                       - 0x0: Disable 16-byte transfer
 *                      - 0x1: Enable 16-byte transfer
 *           - bit 11:   Destination address direction.
 *                       - 0x0: Destination address incremented successively. (default)
 *                       - 0x1: Destination address decremented successively.
 *           - bit 12:   Source address direction.
 *                       - 0x0: Source address incremented successively. (default)
 *                       - 0x1: Source address decremented successively.
 *           - bit 13:   Destination address fixed.
 *                       - 0x0: Change the Destination address during the GDMA
 *                              operation. (default)
 *                       - 0x1: Fixed address is used for each data transfer from
 *                              the destination.
 *           - bit 14:   Source address fixed.
 *                       - 0x0: Change the source address during the GDMA operation. (default)
 *                       - 0x1: Fixed address is used for each data transfer from
 *                              the source.
 */
/** Transfer direction field (bits 6-7) of the channel configuration. */
#define DMA_NPCX_CONFIG_DIR   FIELD(6, 2)
/** Transfer width select field (bits 8-9) of the channel configuration. */
#define DMA_NPCX_CONFIG_TWS   FIELD(8, 2)
/** 16-byte transfer enable bit (bit 10) of the channel configuration. */
#define DMA_NPCX_CONFIG_BME   10
/** Destination address direction field (bit 11) of the channel configuration. */
#define DMA_NPCX_CONFIG_DADIR FIELD(11, 1)
/** Source address direction field (bit 12) of the channel configuration. */
#define DMA_NPCX_CONFIG_SADIR FIELD(12, 1)
/** Destination address fixed bit (bit 13) of the channel configuration. */
#define DMA_NPCX_CONFIG_DAFIX 13
/** Source address fixed bit (bit 14) of the channel configuration. */
#define DMA_NPCX_CONFIG_SAFIX 14

/**
 * Get the config cell of a named dmas element of a DT instance.
 *
 * @param inst Devicetree instance number.
 * @param name Lowercase-and-underscores name of the dmas element as given in dma-names.
 * @return Value of the config cell.
 */
#define NPCX_GDMA_CHANNEL_CONFIG(inst, name) DT_INST_DMAS_CELL_BY_NAME(inst, name, config)
/**
 * Get the transfer direction from a GDMA channel configuration.
 *
 * @param config GDMA channel configuration value.
 * @return Channel direction (see dma.h).
 */
#define NPCX_GDMA_CONFIG_DIRECTION(config)   GET_FIELD(config, DMA_NPCX_CONFIG_DIR)
/**
 * Get the burst length in bytes from a GDMA channel configuration.
 *
 * @param config GDMA channel configuration value.
 * @return Burst length in bytes.
 */
#define NPCX_GDMA_CONFIG_BURST_LENGTH(config)                                                      \
	((1 << GET_FIELD(config, DMA_NPCX_CONFIG_TWS))                                             \
	 << (IS_BIT_SET(config, DMA_NPCX_CONFIG_BME) ? 0x2 : 0x0))
/**
 * Get the destination address adjustment from a GDMA channel configuration.
 *
 * @param config GDMA channel configuration value.
 * @return One of the dma_addr_adj values (see dma.h).
 */
#define NPCX_GDMA_CONFIG_DSTADDR_ADJ(config)                                                       \
	(IS_BIT_SET(config, DMA_NPCX_CONFIG_DAFIX)) ? DMA_ADDR_ADJ_NO_CHANGE                       \
						    : GET_FIELD(config, DMA_NPCX_CONFIG_DADIR)
/**
 * Get the source address adjustment from a GDMA channel configuration.
 *
 * @param config GDMA channel configuration value.
 * @return One of the dma_addr_adj values (see dma.h).
 */
#define NPCX_GDMA_CONFIG_SRCADDR_ADJ(config)                                                       \
	(IS_BIT_SET(config, DMA_NPCX_CONFIG_SAFIX)) ? DMA_ADDR_ADJ_NO_CHANGE                       \
						    : GET_FIELD(config, DMA_NPCX_CONFIG_SADIR)
#endif /* ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_NPCX_GDMA_H_ */
