/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Meshtastic mesh radio stack public API.
 *
 * Implements the on-air packet format used by the Meshtastic project
 * (https://meshtastic.org) on top of Zephyr's raw LoRa driver API.
 * The implementation is wire-compatible with official Meshtastic firmware:
 * same 16-byte packet header, same AES-CTR encryption scheme, same
 * nanopb-encoded @c Data protobuf payload, and same flood-routing algorithm.
 *
 * Minimal feature set: text messaging (PortNum 1) with full flood relay.
 * Use meshtastic_send_data() for other port numbers.
 */

#ifndef ZEPHYR_INCLUDE_MESHTASTIC_MESHTASTIC_H_
#define ZEPHYR_INCLUDE_MESHTASTIC_MESHTASTIC_H_

/**
 * @defgroup meshtastic Meshtastic
 * @ingroup connectivity
 * @{
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Node ID that addresses all nodes simultaneously (broadcast). */
#define MESHTASTIC_NODE_BROADCAST     0xFFFFFFFFU

/**
 * @brief Maximum UTF-8 text payload for a single text message.
 *
 * Limited by the maximum LoRa packet size (255 bytes) minus the 16-byte
 * wire header and the 5-byte protobuf field overhead for the @c Data message.
 */
#define MESHTASTIC_MAX_TEXT_LEN       234U

/** Channel name for the default Meshtastic "LongFast" channel. */
#define MESHTASTIC_CHANNEL_LONGFAST   "LongFast"

/** LoRa carrier frequency for LongFast in the US region (Hz). */
#define MESHTASTIC_FREQ_US            906875000U

/** LoRa carrier frequency for LongFast in the EU region (Hz). */
#define MESHTASTIC_FREQ_EU            869525000U

/**
 * @brief Well-known 16-byte AES-128 PSK for the default LongFast channel.
 *
 * This is the publicly documented default key shared by all Meshtastic
 * devices on the default channel.  It corresponds to the base64 string
 * @c "1PG7OiApB1nwvP+rz05pAQ==" published in the official Meshtastic
 * protobuf repository.
 */
extern const uint8_t meshtastic_default_psk[16];

/**
 * @brief Meshtastic application port numbers.
 *
 * Wire-compatible subset of the official @c PortNum protobuf enum.
 * Only the values required for a messaging-focused implementation are
 * listed; additional values may be passed to meshtastic_send_data() as
 * plain @c uint8_t constants.
 */
enum meshtastic_portnum {
	/** Unknown / unset. */
	MESHTASTIC_PORT_UNKNOWN         = 0,
	/** Plain UTF-8 text message. */
	MESHTASTIC_PORT_TEXT_MESSAGE    = 1,
	/** Remote GPIO / hardware control. */
	MESHTASTIC_PORT_REMOTE_HARDWARE = 2,
	/** GPS position report. */
	MESHTASTIC_PORT_POSITION        = 3,
	/** Node information (name, hardware model, …). */
	MESHTASTIC_PORT_NODEINFO        = 4,
	/** Internal mesh routing. */
	MESHTASTIC_PORT_ROUTING         = 5,
	/** Sensor telemetry. */
	MESHTASTIC_PORT_TELEMETRY       = 67,
	/** Private / application-defined. */
	MESHTASTIC_PORT_PRIVATE         = 256,
};

/**
 * @brief Configuration passed to meshtastic_init().
 *
 * All pointer fields must remain valid for the lifetime of the stack.
 */
struct meshtastic_config {
	/**
	 * Handle of the LoRa transceiver device.  Must be ready before
	 * meshtastic_init() is called.
	 */
	const struct device *lora_dev;

	/**
	 * Local node ID.  Typically derived from the last four bytes of the
	 * device MAC address, but any unique 32-bit value works.  Two nodes
	 * with the same ID on the same mesh will cause routing anomalies.
	 */
	uint32_t node_id;

	/**
	 * Pointer to the AES channel key.  16 bytes for AES-128, 32 bytes
	 * for AES-256.  Use @ref meshtastic_default_psk for the standard
	 * LongFast channel.
	 */
	const uint8_t *psk;

	/** Number of bytes in @p psk (must be 16 or 32). */
	size_t psk_len;

	/**
	 * Channel name used with @p psk to compute the one-byte channel hash
	 * (xor of name and key, per official Meshtastic firmware) embedded in
	 * every packet header.  Use @ref MESHTASTIC_CHANNEL_LONGFAST for the
	 * default channel.
	 */
	const char *channel_name;

	/**
	 * LoRa carrier frequency in Hz.  Use @ref MESHTASTIC_FREQ_US or
	 * @ref MESHTASTIC_FREQ_EU for the default LongFast channel.
	 */
	uint32_t frequency;

	/**
	 * Hop limit written into outgoing packet headers (1–7).
	 * 0 → use @kconfig{CONFIG_MESHTASTIC_DEFAULT_HOP_LIMIT}.
	 */
	uint8_t hop_limit;

	/**
	 * TX power in dBm.
	 * 0 → use @kconfig{CONFIG_MESHTASTIC_TX_POWER}.
	 */
	int8_t tx_power;
};

/**
 * @brief Callback invoked for each received Meshtastic packet.
 *
 * Called from the Meshtastic receive thread after a packet has been
 * successfully decrypted and decoded.  Packets addressed to this node
 * or to the broadcast address trigger the callback; pure relay packets
 * (addressed to another node) do not.
 *
 * @param from        Source node ID.
 * @param to          Destination node ID (@ref MESHTASTIC_NODE_BROADCAST
 *                    for broadcast packets).
 * @param portnum     Application port number (see @ref meshtastic_portnum).
 * @param payload     Decrypted, decoded payload bytes.
 * @param payload_len Number of bytes in @p payload.
 * @param rssi        RSSI in dBm.
 * @param snr         SNR in dB (0.25 dB units as returned by the driver).
 */
typedef void (*meshtastic_recv_cb_t)(uint32_t from, uint32_t to,
				     uint8_t portnum,
				     const uint8_t *payload, size_t payload_len,
				     int16_t rssi, int8_t snr);

/**
 * @brief Initialise the Meshtastic stack.
 *
 * Configures the LoRa modem for the LongFast channel parameters (SF 11,
 * BW 250 kHz, CR 4/5, 16-symbol preamble) at the frequency given in
 * @p cfg, initialises PSA crypto, and starts the background receive
 * thread.  Must be called once before any other @c meshtastic_*() function.
 *
 * @param cfg  Non-NULL pointer to a filled-in configuration structure that
 *             must remain valid for the lifetime of the stack.
 *
 * @retval 0        Success.
 * @retval -EINVAL  @p cfg is NULL or contains invalid parameters.
 * @retval -ENODEV  The LoRa device is not ready.
 * @retval -EIO     LoRa modem configuration or PSA crypto initialisation
 *                  failed.
 */
int meshtastic_init(const struct meshtastic_config *cfg);

/**
 * @brief Send a UTF-8 text message.
 *
 * Encodes @p text as a @c TEXT_MESSAGE_APP (port 1) Meshtastic packet,
 * encrypts it, and transmits it.  Blocks until the radio has finished
 * transmitting.
 *
 * @param dest  Destination node ID, or @ref MESHTASTIC_NODE_BROADCAST.
 * @param text  NUL-terminated UTF-8 string.  Maximum length is
 *              @ref MESHTASTIC_MAX_TEXT_LEN bytes (excluding the NUL).
 *
 * @retval 0        Success.
 * @retval -EINVAL  @p text is NULL or longer than @ref MESHTASTIC_MAX_TEXT_LEN.
 * @retval -ENOMEM  Protobuf encoding failed.
 * @retval -EIO     Crypto or radio transmission failed.
 */
int meshtastic_send_text(uint32_t dest, const char *text);

/**
 * @brief Send a raw Meshtastic data payload.
 *
 * Places @p payload in the @c Data.payload protobuf field, encrypts the
 * message, and transmits it.  Blocks until the radio has finished
 * transmitting.
 *
 * @param dest        Destination node ID, or @ref MESHTASTIC_NODE_BROADCAST.
 * @param portnum     Application port number (see @ref meshtastic_portnum).
 * @param payload     Raw payload bytes.
 * @param payload_len Number of bytes in @p payload (max 237).
 *
 * @retval 0        Success.
 * @retval -EINVAL  Invalid arguments or @p payload_len exceeds the limit.
 * @retval -ENOMEM  Protobuf encoding failed.
 * @retval -EIO     Crypto or radio transmission failed.
 */
int meshtastic_send_data(uint32_t dest, uint8_t portnum,
			 const uint8_t *payload, size_t payload_len);

/**
 * @brief Register (or deregister) a packet-receive callback.
 *
 * Only packets addressed to this node or broadcast are delivered.
 * Pass NULL to deregister the current callback.
 *
 * @param cb  Callback function pointer, or NULL.
 */
void meshtastic_set_recv_cb(meshtastic_recv_cb_t cb);

/**
 * @brief Return the local node ID.
 *
 * @return The @c node_id value supplied in @ref meshtastic_config.
 */
uint32_t meshtastic_get_node_id(void);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* ZEPHYR_INCLUDE_MESHTASTIC_MESHTASTIC_H_ */
