/*
 * Copyright 2023-2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for NXP PXP DMA driver configuration constants.
 * @ingroup dma_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_MCUX_PXP_H_
#define ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_MCUX_PXP_H_

/** Mask of the PXP command in the dma_slot field. */
#define DMA_MCUX_PXP_CMD_MASK  0xE0
/** Bit shift of the PXP command in the dma_slot field. */
#define DMA_MCUX_PXP_CMD_SHIFT 0x5

/** Mask of the pixel format in the dma_slot field. */
#define DMA_MCUX_PXP_FMT_MASK  0x1F
/** Bit shift of the pixel format in the dma_slot field. */
#define DMA_MCUX_PXP_FMT_SHIFT 0x0

/*
 * In order to configure the PXP for rotation, the user should
 * supply a format and command as the DMA slot parameter, like so:
 * dma_slot = (DMA_MCUX_PXP_FTM(DMA_MCUX_PXP_FMT_RGB565) |
 *            DMA_MCUX_PXP_CMD(DMA_MCUX_PXP_CMD_ROTATE_90))
 * head block source address: input buffer address
 * head block destination address: output buffer address
 * source data size: input buffer size in bytes
 * source burst length: height of source buffer in pixels
 * dest data size: output buffer size in bytes
 * dest burst length: height of destination buffer in pixels
 */

/**
 * Encode a pixel format into the dma_slot field.
 *
 * @param x One of the DMA_MCUX_PXP_FMT_x constants.
 * @return Pixel format part of the dma_slot field.
 */
#define DMA_MCUX_PXP_FMT(x) ((x << DMA_MCUX_PXP_FMT_SHIFT) & DMA_MCUX_PXP_FMT_MASK)
/**
 * Encode a PXP command into the dma_slot field.
 *
 * @param x One of the DMA_MCUX_PXP_CMD_x constants.
 * @return Command part of the dma_slot field.
 */
#define DMA_MCUX_PXP_CMD(x) ((x << DMA_MCUX_PXP_CMD_SHIFT) & DMA_MCUX_PXP_CMD_MASK)

#define DMA_MCUX_PXP_CMD_ROTATE_0   0 /**< Do not rotate the frame. */
#define DMA_MCUX_PXP_CMD_ROTATE_90  1 /**< Rotate the frame by 90 degrees. */
#define DMA_MCUX_PXP_CMD_ROTATE_180 2 /**< Rotate the frame by 180 degrees. */
#define DMA_MCUX_PXP_CMD_ROTATE_270 3 /**< Rotate the frame by 270 degrees. */

#define DMA_MCUX_PXP_FMT_RGB565   0 /**< RGB565 pixel format. */
#define DMA_MCUX_PXP_FMT_RGB888   1 /**< RGB888 pixel format. */
#define DMA_MCUX_PXP_FMT_ARGB8888 2 /**< ARGB8888 pixel format. */

/** Mask of the flip setting in the linked_channel field. */
#define DMA_MCUX_PXP_FLIP_MASK  0x3
/** Bit shift of the flip setting in the linked_channel field. */
#define DMA_MCUX_PXP_FLIP_SHIFT 0x0

/*
 * In order to configure the PXP to flip, the user should
 * supply a flip setting as the DMA linked_channel parameter, like so:
 * linked_channel |= DMA_MCUX_PXP_FLIP(DMA_MCUX_PXP_FLIP_HORIZONTAL)
 */

/**
 * Encode a flip setting into the linked_channel field.
 *
 * @param x One of the DMA_MCUX_PXP_FLIP_x constants.
 * @return Flip part of the linked_channel field.
 */
#define DMA_MCUX_PXP_FLIP(x) ((x << DMA_MCUX_PXP_FLIP_SHIFT) & DMA_MCUX_PXP_FLIP_MASK)

#define DMA_MCUX_PXP_FLIP_DISABLE    0 /**< Do not flip the frame. */
#define DMA_MCUX_PXP_FLIP_HORIZONTAL 1 /**< Flip the frame horizontally. */
#define DMA_MCUX_PXP_FLIP_VERTICAL   2 /**< Flip the frame vertically. */
#define DMA_MCUX_PXP_FLIP_BOTH       3 /**< Flip the frame both horizontally and vertically. */

#endif /* ZEPHYR_INCLUDE_DRIVERS_DMA_DMA_MCUX_PXP_H_ */
