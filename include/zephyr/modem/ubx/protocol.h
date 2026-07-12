/*
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief u-blox (UBX) protocol frame definitions and helpers.
 *
 * This header describes the on-the-wire layout of UBX frames along with the
 * message classes, message identifiers and payload structures used by the
 * Zephyr UBX modem module. It also provides helpers to encode frames and to
 * declare constant frames at compile time.
 */

#ifndef ZEPHYR_MODEM_UBX_PROTOCOL_
#define ZEPHYR_MODEM_UBX_PROTOCOL_

#include <stdint.h>
#include <zephyr/modem/ubx/checksum.h>

/**
 * @brief Modem UBX protocol
 * @defgroup modem_ubx_protocol Modem UBX protocol
 * @ingroup modem_ubx
 * @{
 */

/** Size of a UBX frame header (sync chars, class, id and payload size). */
#define UBX_FRAME_HEADER_SZ			6
/** Size of a UBX frame footer (two checksum bytes). */
#define UBX_FRAME_FOOTER_SZ			2
/** Size of a UBX frame excluding its payload. */
#define UBX_FRAME_SZ_WITHOUT_PAYLOAD		(UBX_FRAME_HEADER_SZ + UBX_FRAME_FOOTER_SZ)
/** Total size of a UBX frame carrying a payload of @p payload_size bytes. */
#define UBX_FRAME_SZ(payload_size)		(payload_size + UBX_FRAME_SZ_WITHOUT_PAYLOAD)

/** First preamble synchronization character of a UBX frame. */
#define UBX_PREAMBLE_SYNC_CHAR_1		0xB5
/** Second preamble synchronization character of a UBX frame. */
#define UBX_PREAMBLE_SYNC_CHAR_2		0x62

/** Byte offset of the first preamble synchronization character within a frame. */
#define UBX_FRAME_PREAMBLE_SYNC_CHAR_1_IDX	0
/** Byte offset of the second preamble synchronization character within a frame. */
#define UBX_FRAME_PREAMBLE_SYNC_CHAR_2_IDX	1
/** Byte offset of the message class within a frame. */
#define UBX_FRAME_MSG_CLASS_IDX			2

/** Maximum payload size (in bytes) supported by this implementation. */
#define UBX_PAYLOAD_SZ_MAX			512
/** Maximum total frame size (in bytes) supported by this implementation. */
#define UBX_FRAME_SZ_MAX			UBX_FRAME_SZ(UBX_PAYLOAD_SZ_MAX)

/** @brief UBX frame. */
struct ubx_frame {
	/** First preamble synchronization character (@ref UBX_PREAMBLE_SYNC_CHAR_1). */
	uint8_t preamble_sync_char_1;
	/** Second preamble synchronization character (@ref UBX_PREAMBLE_SYNC_CHAR_2). */
	uint8_t preamble_sync_char_2;
	/** Message class (see @ref ubx_class_id). */
	uint8_t class;
	/** Message identifier within the class. */
	uint8_t id;
	/** Size of the payload, in bytes. */
	uint16_t payload_size;
	/** Payload bytes immediately followed by the two checksum bytes. */
	uint8_t payload_and_checksum[];
};

/** @brief Filter used to match received UBX frames. */
struct ubx_frame_match {
	/** Message class to match (see @ref ubx_class_id). */
	uint8_t class;
	/** Message identifier to match. */
	uint8_t id;
	/** Optional payload to match. */
	struct {
		/** Payload bytes to compare against, or NULL to ignore the payload. */
		uint8_t *buf;
		/** Length of the payload to compare. Set to 0 to match any payload. */
		uint16_t len;
	} payload;
};

/** @brief UBX message class identifiers. */
enum ubx_class_id {
	UBX_CLASS_ID_NAV = 0x01,  /**< Navigation Results Messages */
	UBX_CLASS_ID_RXM = 0x02,  /**< Receiver Manager Messages */
	UBX_CLASS_ID_INF = 0x04,  /**< Information Messages */
	UBX_CLASS_ID_ACK = 0x05,  /**< Ack/Nak Messages */
	UBX_CLASS_ID_CFG = 0x06,  /**< Configuration Input Messages */
	UBX_CLASS_ID_UPD = 0x09,  /**< Firmware Update Messages */
	UBX_CLASS_ID_MON = 0x0A,  /**< Monitoring Messages */
	UBX_CLASS_ID_TIM = 0x0D,  /**< Timing Messages */
	UBX_CLASS_ID_MGA = 0x13,  /**< Multiple GNSS Assistance Messages */
	UBX_CLASS_ID_LOG = 0x21,  /**< Logging Messages */
	UBX_CLASS_ID_SEC = 0x27,  /**< Security Feature Messages */
	UBX_CLASS_ID_NMEA_STD = 0xF0, /**< Standard NMEA messages (only used to configure rate) */
	UBX_CLASS_ID_NMEA_PUBX = 0xF1, /**< Proprietary PUBX messages (only used to configure rate) */
};

/** @brief Message identifiers for the @ref UBX_CLASS_ID_NAV class. */
enum ubx_msg_id_nav {
	UBX_MSG_ID_NAV_PVT = 0x07, /**< Navigation Position Velocity Time solution. */
	UBX_MSG_ID_NAV_SAT = 0x35, /**< Satellite information. */
};

/** @brief Navigation fix types reported in UBX-NAV messages. */
enum ubx_nav_fix_type {
	UBX_NAV_FIX_TYPE_NO_FIX = 0,           /**< No fix. */
	UBX_NAV_FIX_TYPE_DR = 1,               /**< Dead reckoning only. */
	UBX_NAV_FIX_TYPE_2D = 2,               /**< 2D fix. */
	UBX_NAV_FIX_TYPE_3D = 3,               /**< 3D fix. */
	UBX_NAV_FIX_TYPE_GNSS_DR_COMBINED = 4, /**< GNSS and dead reckoning combined. */
	UBX_NAV_FIX_TYPE_TIME_ONLY = 5,        /**< Time-only fix. */
};

/** @brief High-precision / DGNSS solution modes. */
enum ubx_nav_hp_dgnss_mode {
	UBX_NAV_HP_DGNSS_MODE_RTK_FLOAT = 2, /**< RTK with floating ambiguities. */
	UBX_NAV_HP_DGNSS_MODE_RTK_FIXED = 3, /**< RTK with fixed ambiguities. */
};

/** Valid UTC date is available (UBX-NAV-PVT ``valid`` field). */
#define UBX_NAV_PVT_VALID_DATE				BIT(0)
/** Valid UTC time of day is available (UBX-NAV-PVT ``valid`` field). */
#define UBX_NAV_PVT_VALID_TIME				BIT(1)
/** UTC time of day has been fully resolved (UBX-NAV-PVT ``valid`` field). */
#define UBX_NAV_PVT_VALID_UTC_TOD			BIT(2)
/** Valid magnetic declination is available (UBX-NAV-PVT ``valid`` field). */
#define UBX_NAV_PVT_VALID_MAGN				BIT(3)

/** A valid GNSS fix is available (UBX-NAV-PVT ``flags`` field). */
#define UBX_NAV_PVT_FLAGS_GNSS_FIX_OK			BIT(0)
/** Carrier phase solution with floating ambiguities (UBX-NAV-PVT ``flags`` field). */
#define UBX_NAV_PVT_FLAGS_GNSS_CARR_SOLN_FLOATING	BIT(6)
/** Carrier phase solution with fixed ambiguities (UBX-NAV-PVT ``flags`` field). */
#define UBX_NAV_PVT_FLAGS_GNSS_CARR_SOLN_FIXED		BIT(7)

/** Invalid longitude, latitude and height (UBX-NAV-PVT ``flags3`` field). */
#define UBX_NAV_PVT_FLAGS3_INVALID_LLH			BIT(0)

/** @brief Payload of a UBX-NAV-PVT (Position Velocity Time) message. */
struct ubx_nav_pvt {
	/** Time information block. */
	struct {
		/** GPS time of week of the navigation epoch. Milliseconds. */
		uint32_t itow;
		/** Year (UTC). */
		uint16_t year;
		/** Month, range 1..12 (UTC). */
		uint8_t month;
		/** Day of month, range 1..31 (UTC). */
		uint8_t day;
		/** Hour of day, range 0..23 (UTC). */
		uint8_t hour;
		/** Minute of hour, range 0..59 (UTC). */
		uint8_t minute;
		/** Second of minute, range 0..60 (UTC). */
		uint8_t second;
		/** Validity flags (see @c UBX_NAV_PVT_VALID_* macros). */
		uint8_t valid;
		/** Time accuracy estimate (UTC). Nanoseconds. */
		uint32_t tacc;
		/** Fraction of second, range -1e9..1e9 (UTC). Nanoseconds. */
		int32_t nano;
	} __packed time;
	/** GNSS fix type (see @ref ubx_nav_fix_type). */
	uint8_t fix_type;
	/** Fix status flags (see @c UBX_NAV_PVT_FLAGS_* macros). */
	uint8_t flags;
	/** Additional flags. */
	uint8_t flags2;
	/** Navigation solution block. */
	struct {
		/** Number of satellites used in the navigation solution. */
		uint8_t num_sv;
		/** Longitude. Degrees. scaling: 1e-7 */
		int32_t longitude;
		/** Latitude. Degrees. scaling: 1e-7 */
		int32_t latitude;
		/** Height above ellipsoid. mm */
		int32_t height;
		/** Height above mean sea level. mm */
		int32_t hmsl;
		/** Horizontal accuracy estimate. mm */
		uint32_t horiz_acc;
		/** Vertical accuracy estimate. mm */
		uint32_t vert_acc;
		/** NED north velocity. mm/s */
		int32_t vel_north;
		/** NED east velocity. mm/s */
		int32_t vel_east;
		/** NED down velocity. mm/s */
		int32_t vel_down;
		/** Ground Speed (2D). mm/s */
		int32_t ground_speed;
		/** Heading of Motion (2D). Degrees. scaling: 1e-5 */
		int32_t head_motion;
		/** Speed accuracy estimate. mm/s */
		uint32_t speed_acc;
		/** Heading accuracy estimate (both motion and vehicle). Degrees. scaling: 1e-5. */
		uint32_t head_acc;
		/** Position DOP. scaling: 1e-2 */
		uint16_t pdop;
		/** Additional flags (see @c UBX_NAV_PVT_FLAGS3_* macros). */
		uint16_t flags3;
		/** Reserved. */
		uint32_t reserved;
		/** Heading of vehicle (2D). Degrees. Valid if ``flags`` head-vehicle bit is set. */
		int32_t head_vehicle;
		/** Magnetic declination. Degrees. scaling: 1e-2 */
		int16_t mag_decl;
		/** Magnetic declination accuracy. Degrees. scaling: 1e-2 */
		uint16_t magacc;
	} __packed nav;
} __packed;

/** @brief Satellite health status reported in UBX-NAV-SAT. */
enum ubx_nav_sat_health {
	UBX_NAV_SAT_HEALTH_UNKNOWN = 0,   /**< Health unknown. */
	UBX_NAV_SAT_HEALTH_HEALTHY = 1,   /**< Satellite healthy. */
	UBX_NAV_SAT_HEALTH_UNHEALTHY = 2, /**< Satellite unhealthy. */
};

/** @brief GNSS constellation identifiers. */
enum ubx_gnss_id {
	UBX_GNSS_ID_GPS = 0,     /**< GPS. */
	UBX_GNSS_ID_SBAS = 1,    /**< SBAS. */
	UBX_GNSS_ID_GALILEO = 2, /**< Galileo. */
	UBX_GNSS_ID_BEIDOU = 3,  /**< BeiDou. */
	UBX_GNSS_ID_QZSS = 5,    /**< QZSS. */
	UBX_GNSS_ID_GLONASS = 6, /**< GLONASS. */
};

/** Satellite is used in the navigation solution (UBX-NAV-SAT per-satellite flags). */
#define UBX_NAV_SAT_FLAGS_SV_USED			BIT(3)
/** RTCM corrections are used for this satellite (UBX-NAV-SAT per-satellite flags). */
#define UBX_NAV_SAT_FLAGS_RTCM_CORR_USED		BIT(17)

/** @brief Payload of a UBX-NAV-SAT (satellite information) message. */
struct ubx_nav_sat {
	/** GPS time of week of the navigation epoch. Milliseconds. */
	uint32_t itow;
	/** Message version. */
	uint8_t version;
	/** Number of satellites described in the @ref ubx_nav_sat.sat array. */
	uint8_t num_sv;
	/** Reserved. */
	uint16_t reserved1;
	/** @brief Per-satellite information. */
	struct ubx_nav_sat_info {
		/** GNSS constellation identifier (see @ref ubx_gnss_id). */
		uint8_t gnss_id;
		/** Satellite identifier. */
		uint8_t sv_id;
		/** Carrier-to-noise ratio. dBHz */
		uint8_t cno;
		/** Elevation (range: +/- 90). Degrees */
		int8_t elevation;
		/** Azimuth (range: 0 - 360). Degrees */
		int16_t azimuth;
		/** Pseudorange Residual. Meters */
		int16_t pseu_res;
		/** Per-satellite flags (see @c UBX_NAV_SAT_FLAGS_* macros). */
		uint32_t flags;
	} sat[]; /**< Flexible array of per-satellite information entries. */
};

/** @brief Message identifiers for the @ref UBX_CLASS_ID_ACK class. */
enum ubx_msg_id_ack {
	UBX_MSG_ID_ACK = 0x01, /**< Message acknowledged. */
	UBX_MSG_ID_NAK = 0x00  /**< Message not acknowledged. */
};

/** @brief Message identifiers for the @ref UBX_CLASS_ID_CFG class. */
enum ubx_msg_id_cfg {
	UBX_MSG_ID_CFG_PRT = 0x00,     /**< Port configuration. */
	UBX_MSG_ID_CFG_MSG = 0x01,     /**< Message rate configuration. */
	UBX_MSG_ID_CFG_RST = 0x04,     /**< Reset receiver. */
	UBX_MSG_ID_CFG_RATE = 0x08,    /**< Navigation/measurement rate configuration. */
	UBX_MSG_ID_CFG_NAV5 = 0x24,    /**< Navigation engine configuration. */
	UBX_MSG_ID_CFG_VAL_SET = 0x8A, /**< Set configuration values by key. */
	UBX_MSG_ID_CFG_VAL_GET = 0x8B, /**< Get configuration values by key. */
};

/** @brief Message identifiers for the @ref UBX_CLASS_ID_MON class. */
enum ubx_msg_id_mon {
	UBX_MSG_ID_MON_VER = 0x04,  /**< Receiver and software version. */
	UBX_MSG_ID_MON_GNSS = 0x28, /**< Supported/enabled GNSS constellations. */
};

/** @brief Payload of a UBX-ACK-ACK or UBX-ACK-NAK message. */
struct ubx_ack {
	/** Class of the acknowledged/not-acknowledged message. */
	uint8_t class;
	/** Identifier of the acknowledged/not-acknowledged message. */
	uint8_t id;
};

/** GPS constellation bit (UBX-MON-GNSS selection masks). */
#define UBX_GNSS_SELECTION_GPS				BIT(0)
/** GLONASS constellation bit (UBX-MON-GNSS selection masks). */
#define UBX_GNSS_SELECTION_GLONASS			BIT(1)
/** BeiDou constellation bit (UBX-MON-GNSS selection masks). */
#define UBX_GNSS_SELECTION_BEIDOU			BIT(2)
/** Galileo constellation bit (UBX-MON-GNSS selection masks). */
#define UBX_GNSS_SELECTION_GALILEO			BIT(3)

/** @brief Payload of a UBX-MON-GNSS message. */
struct ubx_mon_gnss {
	/** Message version. */
	uint8_t ver;
	/** GNSS constellation selection masks (see @c UBX_GNSS_SELECTION_* macros). */
	struct {
		/** Constellations supported by the receiver. */
		uint8_t supported;
		/** Constellations enabled by default. */
		uint8_t default_enabled;
		/** Constellations currently enabled. */
		uint8_t enabled;
	} selection;
	/** Number of GNSS constellations that can be used simultaneously. */
	uint8_t simultaneous;
	/** Reserved. */
	uint8_t reserved1[3];
} __packed;

/** @brief Port identifiers used by UBX-CFG-PRT. */
enum ubx_cfg_port_id {
	UBX_CFG_PORT_ID_DDC = 0,  /**< DDC (I2C-compatible) port. */
	UBX_CFG_PORT_ID_UART = 1, /**< UART port. */
	UBX_CFG_PORT_ID_USB = 2,  /**< USB port. */
	UBX_CFG_PORT_ID_SPI = 3,  /**< SPI port. */
};

/** @brief UART character length used by UBX-CFG-PRT. */
enum ubx_cfg_char_len {
	UBX_CFG_PRT_PORT_MODE_CHAR_LEN_5 = 0, /**< 5-bit characters (not supported). */
	UBX_CFG_PRT_PORT_MODE_CHAR_LEN_6 = 1, /**< 6-bit characters (not supported). */
	UBX_CFG_PRT_PORT_MODE_CHAR_LEN_7 = 2, /**< 7-bit characters (supported only with parity). */
	UBX_CFG_PRT_PORT_MODE_CHAR_LEN_8 = 3, /**< 8-bit characters. */
};

/** @brief UART parity used by UBX-CFG-PRT. */
enum ubx_cfg_parity {
	UBX_CFG_PRT_PORT_MODE_PARITY_EVEN = 0, /**< Even parity. */
	UBX_CFG_PRT_PORT_MODE_PARITY_ODD = 1,  /**< Odd parity. */
	UBX_CFG_PRT_PORT_MODE_PARITY_NONE = 4, /**< No parity. */
};

/** @brief UART stop bits used by UBX-CFG-PRT. */
enum ubx_cfg_stop_bits {
	UBX_CFG_PRT_PORT_MODE_STOP_BITS_1 = 0,   /**< 1 stop bit. */
	UBX_CFG_PRT_PORT_MODE_STOP_BITS_1_5 = 1, /**< 1.5 stop bits. */
	UBX_CFG_PRT_PORT_MODE_STOP_BITS_2 = 2,   /**< 2 stop bits. */
	UBX_CFG_PRT_PORT_MODE_STOP_BITS_0_5 = 3, /**< 0.5 stop bits. */
};

/** Encode the character length (see @ref ubx_cfg_char_len) into a UBX-CFG-PRT mode. */
#define UBX_CFG_PRT_MODE_CHAR_LEN(val)			(((val) & BIT_MASK(2)) << 6)
/** Encode the parity (see @ref ubx_cfg_parity) into a UBX-CFG-PRT mode. */
#define UBX_CFG_PRT_MODE_PARITY(val)			(((val) & BIT_MASK(3)) << 9)
/** Encode the stop bits (see @ref ubx_cfg_stop_bits) into a UBX-CFG-PRT mode. */
#define UBX_CFG_PRT_MODE_STOP_BITS(val)			(((val) & BIT_MASK(2)) << 12)

/** Enable UBX protocol (UBX-CFG-PRT protocol masks). */
#define UBX_CFG_PRT_PROTO_MASK_UBX			BIT(0)
/** Enable NMEA protocol (UBX-CFG-PRT protocol masks). */
#define UBX_CFG_PRT_PROTO_MASK_NMEA			BIT(1)
/** Enable RTCM 3 protocol (UBX-CFG-PRT protocol masks). */
#define UBX_CFG_PRT_PROTO_MASK_RTCM3			BIT(5)

/** @brief Payload of a UBX-CFG-PRT (port configuration) message. */
struct ubx_cfg_prt {
	/** Port identifier (see @ref ubx_cfg_port_id). */
	uint8_t port_id;
	/** Reserved. */
	uint8_t reserved1;
	/** TX-ready pin configuration. */
	uint16_t rx_ready_pin;
	/** UART mode (see @c UBX_CFG_PRT_MODE_* macros). */
	uint32_t mode;
	/** Baud rate. bits/s */
	uint32_t baudrate;
	/** Input protocol mask (see @c UBX_CFG_PRT_PROTO_MASK_* macros). */
	uint16_t in_proto_mask;
	/** Output protocol mask (see @c UBX_CFG_PRT_PROTO_MASK_* macros). */
	uint16_t out_proto_mask;
	/** Flags. */
	uint16_t flags;
	/** Reserved. */
	uint16_t reserved2;
};

/** @brief Dynamic platform models used by UBX-CFG-NAV5. */
enum ubx_dyn_model {
	UBX_DYN_MODEL_PORTABLE = 0,    /**< Portable. */
	UBX_DYN_MODEL_STATIONARY = 2,  /**< Stationary. */
	UBX_DYN_MODEL_PEDESTRIAN = 3,  /**< Pedestrian. */
	UBX_DYN_MODEL_AUTOMOTIVE = 4,  /**< Automotive. */
	UBX_DYN_MODEL_SEA = 5,         /**< Sea. */
	UBX_DYN_MODEL_AIRBORNE_1G = 6, /**< Airborne with < 1g acceleration. */
	UBX_DYN_MODEL_AIRBORNE_2G = 7, /**< Airborne with < 2g acceleration. */
	UBX_DYN_MODEL_AIRBORNE_4G = 8, /**< Airborne with < 4g acceleration. */
	UBX_DYN_MODEL_WRIST = 9,       /**< Wrist-worn. */
	UBX_DYN_MODEL_BIKE = 10,       /**< Motorbike. */
};

/** @brief Position fixing modes used by UBX-CFG-NAV5. */
enum ubx_fix_mode {
	UBX_FIX_MODE_2D_ONLY = 1, /**< 2D only. */
	UBX_FIX_MODE_3D_ONLY = 2, /**< 3D only. */
	UBX_FIX_MODE_AUTO = 3,    /**< Auto 2D/3D. */
};

/** @brief UTC standards selectable through UBX-CFG-NAV5. */
enum ubx_utc_standard {
	UBX_UTC_STANDARD_AUTOMATIC = 0, /**< Automatic selection. */
	UBX_UTC_STANDARD_GPS = 3,       /**< UTC as operated by the USNO (GPS). */
	UBX_UTC_STANDARD_GALILEO = 5,   /**< UTC as operated by Galileo. */
	UBX_UTC_STANDARD_GLONASS = 6,   /**< UTC as operated by GLONASS (SU). */
	UBX_UTC_STANDARD_BEIDOU = 7,    /**< UTC as operated by BeiDou (NTSC). */
};

/** Apply the dynamic platform model (UBX-CFG-NAV5 ``apply`` mask). */
#define UBX_CFG_NAV5_APPLY_DYN			BIT(0)
/** Apply the fix mode (UBX-CFG-NAV5 ``apply`` mask). */
#define UBX_CFG_NAV5_APPLY_FIX_MODE		BIT(2)

/** @brief Payload of a UBX-CFG-NAV5 (navigation engine settings) message. */
struct ubx_cfg_nav5 {
	/** Mask of parameters to apply (see @c UBX_CFG_NAV5_APPLY_* macros). */
	uint16_t apply;
	/** Dynamic platform model (see @ref ubx_dyn_model). */
	uint8_t dyn_model;
	/** Position fixing mode (see @ref ubx_fix_mode). */
	uint8_t fix_mode;
	/** Fixed altitude for 2D fix mode. Meters */
	int32_t fixed_alt;
	/** Variance for fixed altitude in 2D mode. Sq. meters */
	uint32_t fixed_alt_var;
	/** Minimum elevation to use a GNSS satellite in navigation. Degrees */
	int8_t min_elev;
	/** Reserved. */
	uint8_t dr_limit;
	/** Position DOP mask. */
	uint16_t p_dop;
	/** Time DOP mask. */
	uint16_t t_dop;
	/** Position accuracy mask. Meters */
	uint16_t p_acc;
	/** Time accuracy mask. Meters */
	uint16_t t_acc;
	/** Static hold threshold. cm/s */
	uint8_t static_hold_thresh;
	/** DGNSS timeout. Seconds */
	uint8_t dgnss_timeout;
	/** Number of satellites required above @ref ubx_cfg_nav5.cno_thresh. */
	uint8_t cno_thresh_num_svs;
	/** C/N0 threshold for GNSS signals. dBHz */
	uint8_t cno_thresh;
	/** Reserved. */
	uint8_t reserved1[2];
	/** Static hold distance threshold. Meters */
	uint16_t static_hold_max_dist;
	/** UTC standard to be used (see @ref ubx_utc_standard). */
	uint8_t utc_standard;
	/** Reserved. */
	uint8_t reserved2[5];
} __packed;

/** @brief Start modes used by UBX-CFG-RST. */
enum ubx_cfg_rst_start_mode {
	UBX_CFG_RST_HOT_START = 0x0000,  /**< Hot start (retain all data). */
	UBX_CFG_RST_WARM_START = 0x0001, /**< Warm start (clear ephemeris). */
	UBX_CFG_RST_COLD_START = 0xFFFF, /**< Cold start (clear all data). */
};

/** @brief Reset modes used by UBX-CFG-RST. */
enum ubx_cfg_rst_mode {
	UBX_CFG_RST_MODE_HW = 0x00,         /**< Hardware reset (watchdog) immediately. */
	UBX_CFG_RST_MODE_SW = 0x01,         /**< Controlled software reset. */
	UBX_CFG_RST_MODE_GNSS_STOP = 0x08,  /**< Controlled GNSS stop. */
	UBX_CFG_RST_MODE_GNSS_START = 0x09, /**< Controlled GNSS start. */
};

/** @brief Payload of a UBX-CFG-RST (reset receiver) message. */
struct ubx_cfg_rst {
	/** Battery-backed RAM sections to clear on reset. */
	uint16_t nav_bbr_mask;
	/** Reset mode (see @ref ubx_cfg_rst_mode). */
	uint8_t reset_mode;
	/** Reserved. */
	uint8_t reserved;
};

/** @brief Time reference used by UBX-CFG-RATE. */
enum ubx_cfg_rate_time_ref {
	UBX_CFG_RATE_TIME_REF_UTC = 0,     /**< Align measurements to UTC time. */
	UBX_CFG_RATE_TIME_REF_GPS = 1,     /**< Align measurements to GPS time. */
	UBX_CFG_RATE_TIME_REF_GLONASS = 2, /**< Align measurements to GLONASS time. */
	UBX_CFG_RATE_TIME_REF_BEIDOU = 3,  /**< Align measurements to BeiDou time. */
	UBX_CFG_RATE_TIME_REF_GALILEO = 4, /**< Align measurements to Galileo time. */
	UBX_CFG_RATE_TIME_REF_NAVIC = 5,   /**< Align measurements to NavIC time. */
};

/** @brief Payload of a UBX-CFG-RATE (navigation/measurement rate) message. */
struct ubx_cfg_rate {
	/** Nominal time between GNSS measurements. Milliseconds */
	uint16_t meas_rate_ms;
	/** Ratio of number of measurements to number of navigation solutions. */
	uint16_t nav_rate;
	/** Time system to which measurements are aligned (see @ref ubx_cfg_rate_time_ref). */
	uint16_t time_ref;
};

/** @brief Version of the UBX-CFG-VALSET/VALGET message. */
enum ubx_cfg_val_ver {
	UBX_CFG_VAL_VER_SIMPLE = 0,      /**< Simple (non-transactional) configuration. */
	UBX_CFG_VAL_VER_TRANSACTION = 1, /**< Transactional configuration. */
};

/** @brief Header of a UBX-CFG-VALSET/VALGET message. */
struct ubx_cfg_val_hdr {
	/** Message version (see @ref ubx_cfg_val_ver). */
	uint8_t ver;
	/** Configuration layer(s) to which the operation applies. */
	uint8_t layer;
	/** Position within a transaction. */
	uint16_t position;
} __packed;

/** @brief UBX-CFG-VALSET message carrying a single 8-bit configuration value. */
struct ubx_cfg_val_u8 {
	/** Message header. */
	struct ubx_cfg_val_hdr hdr;
	/** Configuration key (see @ref modem_ubx_keys). */
	uint32_t key;
	/** Configuration value. */
	uint8_t value;
} __packed;

/** @brief UBX-CFG-VALSET message carrying a single 16-bit configuration value. */
struct ubx_cfg_val_u16 {
	/** Message header. */
	struct ubx_cfg_val_hdr hdr;
	/** Configuration key (see @ref modem_ubx_keys). */
	uint32_t key;
	/** Configuration value. */
	uint16_t value;
} __packed;

/** @brief UBX-CFG-VALSET message carrying a single 32-bit configuration value. */
struct ubx_cfg_val_u32 {
	/** Message header. */
	struct ubx_cfg_val_hdr hdr;
	/** Configuration key (see @ref modem_ubx_keys). */
	uint32_t key;
	/** Configuration value. */
	uint32_t value;
} __packed;

/** @brief Message identifiers for the @ref UBX_CLASS_ID_NMEA_STD class. */
enum ubx_msg_id_nmea_std {
	UBX_MSG_ID_NMEA_STD_DTM = 0x0A, /**< Datum reference. */
	UBX_MSG_ID_NMEA_STD_GBQ = 0x44, /**< Poll a standard message (Talker ID GB). */
	UBX_MSG_ID_NMEA_STD_GBS = 0x09, /**< GNSS satellite fault detection. */
	UBX_MSG_ID_NMEA_STD_GGA = 0x00, /**< Global positioning system fix data. */
	UBX_MSG_ID_NMEA_STD_GLL = 0x01, /**< Latitude and longitude, with time and status. */
	UBX_MSG_ID_NMEA_STD_GLQ = 0x43, /**< Poll a standard message (Talker ID GL). */
	UBX_MSG_ID_NMEA_STD_GNQ = 0x42, /**< Poll a standard message (Talker ID GN). */
	UBX_MSG_ID_NMEA_STD_GNS = 0x0D, /**< GNSS fix data. */
	UBX_MSG_ID_NMEA_STD_GPQ = 0x40, /**< Poll a standard message (Talker ID GP). */
	UBX_MSG_ID_NMEA_STD_GRS = 0x06, /**< GNSS range residuals. */
	UBX_MSG_ID_NMEA_STD_GSA = 0x02, /**< GNSS DOP and active satellites. */
	UBX_MSG_ID_NMEA_STD_GST = 0x07, /**< GNSS pseudorange error statistics. */
	UBX_MSG_ID_NMEA_STD_GSV = 0x03, /**< GNSS satellites in view. */
	UBX_MSG_ID_NMEA_STD_RMC = 0x04, /**< Recommended minimum data. */
	UBX_MSG_ID_NMEA_STD_THS = 0x0E, /**< True heading and status. */
	UBX_MSG_ID_NMEA_STD_TXT = 0x41, /**< Text transmission. */
	UBX_MSG_ID_NMEA_STD_VLW = 0x0F, /**< Dual ground/water distance. */
	UBX_MSG_ID_NMEA_STD_VTG = 0x05, /**< Course over ground and ground speed. */
	UBX_MSG_ID_NMEA_STD_ZDA = 0x08, /**< Time and date. */
};

/** @brief Message identifiers for the @ref UBX_CLASS_ID_NMEA_PUBX class. */
enum ubx_msg_id_nmea_pubx {
	UBX_MSG_ID_NMEA_PUBX_CONFIG = 0x41,   /**< Set protocols and baud rate. */
	UBX_MSG_ID_NMEA_PUBX_POSITION = 0x00, /**< Position, velocity and status. */
	UBX_MSG_ID_NMEA_PUBX_RATE = 0x40,     /**< Set NMEA message output rate. */
	UBX_MSG_ID_NMEA_PUBX_SVSTATUS = 0x03, /**< Satellite status. */
	UBX_MSG_ID_NMEA_PUBX_TIME = 0x04,     /**< Time of day and clock information. */
};

/** @brief Payload of a UBX-CFG-MSG (message rate configuration) message. */
struct ubx_cfg_msg_rate {
	/** Class of the message whose rate is configured (see @ref ubx_class_id). */
	uint8_t class;
	/** Identifier of the message whose rate is configured. */
	uint8_t id;
	/** Output rate, in number of navigation solutions between transmissions. */
	uint8_t rate;
};

/** @brief Payload of a UBX-MON-VER (receiver/software version) message. */
struct ubx_mon_ver {
	/** Nul-terminated software version string. */
	char sw_ver[30];
	/** Nul-terminated hardware version string. */
	char hw_ver[10];
};

/**
 * @brief Compute the UBX checksum of a frame.
 *
 * @param frame Frame over which the checksum is computed
 * @param len Total length of the frame, in bytes
 * @return 16-bit checksum with CK_A in the low byte and CK_B in the high byte,
 *         or 0xFFFF if @p len is inconsistent with the frame's payload size
 */
static inline uint16_t ubx_calc_checksum(const struct ubx_frame *frame, size_t len)
{
	uint8_t ck_a = 0;
	uint8_t ck_b = 0;
	const uint8_t *data = (const uint8_t *)frame;

	/** Mismatch in expected and actual length results in an invalid frame */
	if (len != UBX_FRAME_SZ(frame->payload_size)) {
		return 0xFFFF;
	}

	for (int i = UBX_FRAME_MSG_CLASS_IDX ; i < (UBX_FRAME_SZ(frame->payload_size) - 2) ; i++) {
		ck_a = ck_a + data[i];
		ck_b = ck_b + ck_a;
	}

	return ((ck_a & 0xFF) | ((ck_b & 0xFF) << 8));
}

/**
 * @brief Encode a UBX frame into a buffer.
 *
 * Builds a complete UBX frame (preamble, class, id, payload and checksum) into
 * @p buf from the provided class, id and payload.
 *
 * @param class Message class (see @ref ubx_class_id)
 * @param id Message identifier
 * @param payload Payload bytes to copy into the frame (may be NULL if @p payload_len is 0)
 * @param payload_len Length of the payload, in bytes
 * @param buf Destination buffer
 * @param buf_len Size of the destination buffer, in bytes
 * @return Total length of the encoded frame in bytes if successful
 * @return -EINVAL if @p buf_len is too small to hold the frame
 */
static inline int ubx_frame_encode(uint8_t class, uint8_t id,
				    const uint8_t *payload, size_t payload_len,
				    uint8_t *buf, size_t buf_len)
{
	if (buf_len < UBX_FRAME_SZ(payload_len)) {
		return -EINVAL;
	}

	struct ubx_frame *frame = (struct ubx_frame *)buf;

	frame->preamble_sync_char_1 = UBX_PREAMBLE_SYNC_CHAR_1;
	frame->preamble_sync_char_2 = UBX_PREAMBLE_SYNC_CHAR_2;
	frame->class = class;
	frame->id = id;
	frame->payload_size = payload_len;
	memcpy(frame->payload_and_checksum, payload, payload_len);

	uint16_t checksum = ubx_calc_checksum(frame, UBX_FRAME_SZ(payload_len));

	frame->payload_and_checksum[payload_len] = checksum & 0xFF;
	frame->payload_and_checksum[payload_len + 1] = (checksum >> 8) & 0xFF;

	return UBX_FRAME_SZ(payload_len);
}

/**
 * @brief Define a constant @ref ubx_frame instance.
 *
 * @param _name Name of the frame variable to declare
 * @param _frame Frame initializer (e.g. one of the @c UBX_FRAME_*_INITIALIZER macros)
 */
#define UBX_FRAME_DEFINE(_name, _frame)								   \
	const static struct ubx_frame _name = _frame

/**
 * @brief Define a constant array of pointers to @ref ubx_frame instances.
 *
 * @param _name Name of the array variable to declare
 * @param ... Pointers to the frames that populate the array
 */
#define UBX_FRAME_ARRAY_DEFINE(_name, ...)							   \
	const struct ubx_frame *_name[] = {__VA_ARGS__};

/**
 * @brief Initializer for a UBX-ACK-ACK frame.
 *
 * @param _class_id Class of the acknowledged message
 * @param _msg_id Identifier of the acknowledged message
 */
#define UBX_FRAME_ACK_INITIALIZER(_class_id, _msg_id)						   \
	UBX_FRAME_INITIALIZER_PAYLOAD(UBX_CLASS_ID_ACK, UBX_MSG_ID_ACK, _class_id, _msg_id)

/**
 * @brief Initializer for a UBX-ACK-NAK frame.
 *
 * @param _class_id Class of the not-acknowledged message
 * @param _msg_id Identifier of the not-acknowledged message
 */
#define UBX_FRAME_NAK_INITIALIZER(_class_id, _msg_id)						   \
	UBX_FRAME_INITIALIZER_PAYLOAD(UBX_CLASS_ID_ACK, UBX_MSG_ID_NAK, _class_id, _msg_id)

/**
 * @brief Initializer for a UBX-CFG-RST frame.
 *
 * @param _start_mode Start mode (see @ref ubx_cfg_rst_start_mode)
 * @param _reset_mode Reset mode (see @ref ubx_cfg_rst_mode)
 */
#define UBX_FRAME_CFG_RST_INITIALIZER(_start_mode, _reset_mode)					   \
	UBX_FRAME_INITIALIZER_PAYLOAD(UBX_CLASS_ID_CFG, UBX_MSG_ID_CFG_RST,			   \
				      (_start_mode & 0xFF), ((_start_mode >> 8) & 0xFF),	   \
				       _reset_mode, 0)

/**
 * @brief Initializer for a UBX-CFG-RATE frame.
 *
 * @param _meas_rate_ms Nominal time between GNSS measurements, in milliseconds
 * @param _nav_rate Ratio of measurements to navigation solutions
 * @param _time_ref Time reference (see @ref ubx_cfg_rate_time_ref)
 */
#define UBX_FRAME_CFG_RATE_INITIALIZER(_meas_rate_ms, _nav_rate, _time_ref)			   \
	UBX_FRAME_INITIALIZER_PAYLOAD(UBX_CLASS_ID_CFG, UBX_MSG_ID_CFG_RATE,			   \
				      (_meas_rate_ms & 0xFF), ((_meas_rate_ms >> 8) & 0xFF),	   \
				      (_nav_rate & 0xFF), ((_nav_rate >> 8) & 0xFF),		   \
				      (_time_ref & 0xFF), ((_time_ref >> 8) & 0xFF))

/**
 * @brief Initializer for a UBX-CFG-MSG frame setting a message output rate.
 *
 * @param _class_id Class of the message whose rate is configured
 * @param _msg_id Identifier of the message whose rate is configured
 * @param _rate Output rate, in number of navigation solutions between transmissions
 */
#define UBX_FRAME_CFG_MSG_RATE_INITIALIZER(_class_id, _msg_id, _rate)				   \
	UBX_FRAME_INITIALIZER_PAYLOAD(UBX_CLASS_ID_CFG, UBX_MSG_ID_CFG_MSG,			   \
				      _class_id, _msg_id, _rate)

/**
 * @brief Initializer for a UBX-CFG-VALSET frame setting an 8-bit value.
 *
 * @param _key Configuration key (see @ref modem_ubx_keys)
 * @param _value 8-bit configuration value
 */
#define UBX_FRAME_CFG_VAL_SET_U8_INITIALIZER(_key, _value)					   \
	UBX_FRAME_INITIALIZER_PAYLOAD(UBX_CLASS_ID_CFG, UBX_MSG_ID_CFG_VAL_SET,			   \
				      0x00, 0x01, 0x00, 0x00,					   \
				      ((_key) & 0xFF), (((_key) >> 8) & 0xFF),			   \
				      (((_key) >> 16) & 0xFF), (((_key) >> 24) & 0xFF),		   \
				      ((_value) & 0xFF))

/**
 * @brief Initializer for a UBX-CFG-VALSET frame setting a 16-bit value.
 *
 * @param _key Configuration key (see @ref modem_ubx_keys)
 * @param _value 16-bit configuration value
 */
#define UBX_FRAME_CFG_VAL_SET_U16_INITIALIZER(_key, _value)					   \
	UBX_FRAME_INITIALIZER_PAYLOAD(UBX_CLASS_ID_CFG, UBX_MSG_ID_CFG_VAL_SET,			   \
				      0x00, 0x01, 0x00, 0x00,					   \
				      ((_key) & 0xFF), (((_key) >> 8) & 0xFF),			   \
				      (((_key) >> 16) & 0xFF), (((_key) >> 24) & 0xFF),		   \
				      ((_value) & 0xFF), (((_value) >> 8) & 0xFF))

/**
 * @brief Initializer for a UBX-CFG-VALSET frame setting a 32-bit value.
 *
 * @param _key Configuration key (see @ref modem_ubx_keys)
 * @param _value 32-bit configuration value
 */
#define UBX_FRAME_CFG_VAL_SET_U32_INITIALIZER(_key, _value)					   \
	UBX_FRAME_INITIALIZER_PAYLOAD(UBX_CLASS_ID_CFG, UBX_MSG_ID_CFG_VAL_SET,			   \
				      0x00, 0x01, 0x00, 0x00,					   \
				      ((_key) & 0xFF), (((_key) >> 8) & 0xFF),			   \
				      (((_key) >> 16) & 0xFF), (((_key) >> 24) & 0xFF),		   \
				      ((_value) & 0xFF), (((_value) >> 8) & 0xFF),		   \
				      (((_value) >> 16) & 0xFF), (((_value) >> 24) & 0xFF))

/**
 * @brief Initializer for a UBX-CFG-VALGET frame requesting a value.
 *
 * @param _key Configuration key (see @ref modem_ubx_keys)
 */
#define UBX_FRAME_CFG_VAL_GET_INITIALIZER(_key)							   \
	UBX_FRAME_INITIALIZER_PAYLOAD(UBX_CLASS_ID_CFG, UBX_MSG_ID_CFG_VAL_GET,			   \
				      0x00, 0x00, 0x00, 0x00,					   \
				      ((_key) & 0xFF), (((_key) >> 8) & 0xFF),			   \
				      (((_key) >> 16) & 0xFF), (((_key) >> 24) & 0xFF))

/**
 * @brief Initializer for a UBX frame carrying an explicit payload.
 *
 * The payload checksum is computed at compile time.
 *
 * @param _class_id Message class (see @ref ubx_class_id)
 * @param _msg_id Message identifier
 * @param ... Payload bytes
 */
#define UBX_FRAME_INITIALIZER_PAYLOAD(_class_id, _msg_id, ...)					   \
	_UBX_FRAME_INITIALIZER_PAYLOAD(_class_id, _msg_id, __VA_ARGS__)

/** @cond INTERNAL_HIDDEN */

#define _UBX_FRAME_INITIALIZER_PAYLOAD(_class_id, _msg_id, ...)					   \
	{											   \
		.preamble_sync_char_1 = UBX_PREAMBLE_SYNC_CHAR_1,				   \
		.preamble_sync_char_2 = UBX_PREAMBLE_SYNC_CHAR_2,				   \
		.class = _class_id,								   \
		.id = _msg_id,									   \
		.payload_size = (NUM_VA_ARGS(__VA_ARGS__)) & 0xFFFF,				   \
		.payload_and_checksum = {							   \
			__VA_ARGS__,								   \
			UBX_CSUM(_class_id, _msg_id,						   \
				 ((NUM_VA_ARGS(__VA_ARGS__)) & 0xFF),				   \
				 (((NUM_VA_ARGS(__VA_ARGS__)) >> 8) & 0xFF),			   \
				 __VA_ARGS__),							   \
		},										   \
	}

/** @endcond */

/**
 * @brief Initializer for a UBX "get"/"poll" frame with an empty payload.
 *
 * @param _class_id Message class (see @ref ubx_class_id)
 * @param _msg_id Message identifier
 */
#define UBX_FRAME_GET_INITIALIZER(_class_id, _msg_id)						   \
	{											   \
		.preamble_sync_char_1 = UBX_PREAMBLE_SYNC_CHAR_1,				   \
		.preamble_sync_char_2 = UBX_PREAMBLE_SYNC_CHAR_2,				   \
		.class = _class_id,								   \
		.id = _msg_id,									   \
		.payload_size = 0,								   \
		.payload_and_checksum = {							   \
			UBX_CSUM(_class_id, _msg_id, 0, 0),					   \
		},										   \
	}

/** @} */

#endif /* ZEPHYR_MODEM_UBX_PROTOCOL_ */
