/*
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Configuration keys for the u-blox (UBX) generation 9 configuration interface.
 *
 * Devices supporting the UBX generation 9 configuration interface (UBX-CFG-VALSET /
 * UBX-CFG-VALGET) are configured through numeric keys rather than dedicated messages.
 * This header enumerates the keys used by the Zephyr UBX modem module.
 */

#ifndef ZEPHYR_MODEM_UBX_KEYS_
#define ZEPHYR_MODEM_UBX_KEYS_

/**
 * @brief Modem UBX configuration keys
 * @defgroup modem_ubx_keys Modem UBX configuration keys
 * @ingroup modem_ubx
 * @{
 */

/** @brief Message output rate configuration keys. */
enum ubx_keys_msg_out {
	/** Output rate of the NMEA-GGA message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_GGA_UART1 = 0x209100bb,
	/** Output rate of the NMEA-RMC message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_RMC_UART1 = 0x209100ac,
	/** Output rate of the NMEA-GSV message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_GSV_UART1 = 0x209100c5,
	/** Output rate of the NMEA-DTM message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_DTM_UART1 = 0x209100a7,
	/** Output rate of the NMEA-GBS message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_GBS_UART1 = 0x209100de,
	/** Output rate of the NMEA-GLL message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_GLL_UART1 = 0x209100ca,
	/** Output rate of the NMEA-GNS message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_GNS_UART1 = 0x209100b6,
	/** Output rate of the NMEA-GRS message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_GRS_UART1 = 0x209100cf,
	/** Output rate of the NMEA-GSA message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_GSA_UART1 = 0x209100c0,
	/** Output rate of the NMEA-GST message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_GST_UART1 = 0x209100d4,
	/** Output rate of the NMEA-VTG message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_VTG_UART1 = 0x209100b1,
	/** Output rate of the NMEA-VLW message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_VLW_UART1 = 0x209100e8,
	/** Output rate of the NMEA-ZDA message on UART1. */
	UBX_KEY_MSG_OUT_NMEA_ZDA_UART1 = 0x209100d9,
	/** Output rate of the UBX-NAV-PVT message on UART1. */
	UBX_KEY_MSG_OUT_UBX_NAV_PVT_UART1 = 0x20910007,
	/** Output rate of the UBX-NAV-SAT message on UART1. */
	UBX_KEY_MSG_OUT_UBX_NAV_SAT_UART1 = 0x20910016,
	/** Output rate of the UBX-RXM-RTCM message on UART1. */
	UBX_KEY_MSG_OUT_UBX_RXM_RTCM_UART1 = 0x20910269,
};

/** @brief Measurement and navigation rate configuration keys. */
enum ubx_keys_rate {
	/** Nominal time between GNSS measurements, in milliseconds. */
	UBX_KEY_RATE_MEAS = 0x30210001,
	/** Ratio of number of measurements to number of navigation solutions. */
	UBX_KEY_RATE_NAV = 0x30210002,
};

/** @brief Navigation engine configuration keys. */
enum ubx_keys_nav_cfg {
	/** Position fixing mode (see @ref ubx_fix_mode). */
	UBX_KEY_NAV_CFG_FIX_MODE = 0x20110011,
	/** Dynamic platform model (see @ref ubx_dyn_model). */
	UBX_KEY_NAV_CFG_DYN_MODEL = 0x20110021,
};

/** @brief UART1 input/output protocol configuration keys. */
enum ubx_keys_uart1_proto {
	/** Enable UBX protocol on the UART1 input. */
	UBX_KEY_UART1_PROTO_IN_UBX = 0x10730001,
	/** Enable NMEA protocol on the UART1 input. */
	UBX_KEY_UART1_PROTO_IN_NMEA = 0x10730002,
	/** Enable RTCM 3.x protocol on the UART1 input. */
	UBX_KEY_UART1_PROTO_IN_RTCM3X = 0x10730004,
	/** Enable UBX protocol on the UART1 output. */
	UBX_KEY_UART1_PROTO_OUT_UBX = 0x10740001,
	/** Enable NMEA protocol on the UART1 output. */
	UBX_KEY_UART1_PROTO_OUT_NMEA = 0x10740002,
	/** Enable RTCM 3.x protocol on the UART1 output. */
	UBX_KEY_UART1_PROTO_OUT_RTCM3X = 0x10740004,
};

/** @brief UART1 configuration keys. */
enum ubx_keys_uart1_cfg {
	UBX_KEY_CFG_UART1_BAUDRATE = 0x40520001, /**< default 38400 */
	UBX_KEY_CFG_UART1_STOPBITS = 0x20520002, /**< default 1 (ONE) */
	UBX_KEY_CFG_UART1_DATABITS = 0x20520003, /**< default 0 (EIGHT) */
	UBX_KEY_CFG_UART1_PARITY = 0x20520004, /**< default 0 (NONE) */
	UBX_KEY_CFG_UART1_ENABLED = 0x10520005, /**< default 1 (true) */
};

/** @brief High-precision navigation configuration keys. */
enum ubx_keys_nav_hp_cfg {
	/** DGNSS / high-precision GNSS mode (see @ref ubx_nav_hp_dgnss_mode). */
	UBX_KEY_NAV_HP_CFG_GNSS_MODE = 0x20140011,
};

/** @} */

#endif /* ZEPHYR_MODEM_UBX_KEYS_ */
