/*
 * Copyright 2023,2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the NXP MCUX DSI 2L peripheral specific message flags.
 * @ingroup mipi_dsi_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_MCUX_2L_
#define ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_MCUX_2L_

/**
 * @brief Flag message payload as framebuffer data.
 *
 * HW specific flag. Indicates to the MIPI DSI 2L peripheral that the
 * data being sent is framebuffer data, which the DSI peripheral may
 * byte swap depending on Kconfig settings.
 */
#define MCUX_DSI_2L_FB_DATA BIT(0x1)

/**
 * @brief Enter ULPS after the transfer.
 *
 * HW specific flag. When set in the message flags, the bus enters the
 * ultra-low power state (ULPS) after the transfer completes.
 */
#define MCUX_DSI_2L_ULPS BIT(0x2)

#endif /* ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_MCUX_2L_ */
