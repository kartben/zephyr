/*
 * Copyright 2023,2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mipi_dsi_interface
 * @brief NXP MIPI-DSI 2L message flags.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_MCUX_2L_
#define ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_MCUX_2L_

/**
 * @brief Indicate framebuffer payload data.
 *
 * Set this flag in @ref mipi_dsi_msg.flags to identify payload data as
 * framebuffer content. The controller can perform byte-swapping according to
 * Kconfig settings.
 */
#define MCUX_DSI_2L_FB_DATA BIT(0x1)

/**
 * @brief Enter ULPS after transfer.
 *
 * Set this flag in @ref mipi_dsi_msg.flags to request ULPS entry after the
 * transfer completes.
 */
#define MCUX_DSI_2L_ULPS BIT(0x2)

#endif /* ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_MCUX_2L_ */
