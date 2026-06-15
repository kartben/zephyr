/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Reset encoding helper for NXP SYSCON reset controllers
 * @ingroup reset_controller_nxp
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_RESET_NXP_SYSCON_RESET_COMMON_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_RESET_NXP_SYSCON_RESET_COMMON_H_

/**
 * @defgroup reset_controller_nxp NXP reset controller helpers
 * @brief NXP reset controller helpers
 * @ingroup reset_controller_interface
 *
 * @details Devicetree macro for encoding peripheral resets on NXP SYSCON-based reset
 * controllers, for use with the <tt>nxp,lpc-syscon-reset</tt> binding.
 * @{
 */

/**
 * @brief Encode a SYSCON reset register offset and bit.
 *
 * @param offset Reset register offset.
 * @param bit Reset bit within the register.
 */
#define NXP_SYSCON_RESET(offset, bit) ((offset << 16) | bit)

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_RESET_NXP_SYSCON_RESET_COMMON_H_ */
