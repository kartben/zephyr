/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup scmi_interface
 * @brief Header file for the SCMI (System Control and Management Interface) driver API.
 */

#ifndef _INCLUDE_ZEPHYR_DRIVERS_FIRMWARE_SCMI_PROTOCOL_H_
#define _INCLUDE_ZEPHYR_DRIVERS_FIRMWARE_SCMI_PROTOCOL_H_

/**
 * @brief Interfaces for ARM System Control and Management Interface (SCMI)
 * @defgroup scmi_interface SCMI
 * @ingroup io_interfaces
 * @{
 */

#include <zephyr/device.h>
#include <zephyr/drivers/firmware/scmi/util.h>
#include <stdint.h>
#include <errno.h>

/**
 * @brief Build an SCMI message header
 *
 * Builds an SCMI message header based on the
 * fields that make it up.
 *
 * @param id message ID
 * @param type message type
 * @param proto protocol ID
 * @param token message token
 */
#define SCMI_MESSAGE_HDR_MAKE(id, type, proto, token)	\
	(SCMI_FIELD_MAKE(id, GENMASK(7, 0), 0)     |	\
	 SCMI_FIELD_MAKE(type, GENMASK(1, 0), 8)   |	\
	 SCMI_FIELD_MAKE(proto, GENMASK(7, 0), 10) |	\
	 SCMI_FIELD_MAKE(token, GENMASK(9, 0), 18))

struct scmi_channel;

/**
 * @brief SCMI message type
 */
enum scmi_message_type {
	/** command message */
	SCMI_COMMAND = 0x0,
	/** delayed reply message */
	SCMI_DELAYED_REPLY = 0x2,
	/** notification message */
	SCMI_NOTIFICATION = 0x3,
};

/**
 * @brief SCMI status codes
 */
enum scmi_status_code {
	/** Successful completion of the command */
	SCMI_SUCCESS = 0,
	/** The command or feature is not supported */
	SCMI_NOT_SUPPORTED = -1,
	/** One or more parameters passed to the command are invalid */
	SCMI_INVALID_PARAMETERS = -2,
	/** The caller is not permitted to perform the specific action */
	SCMI_DENIED = -3,
	/** The entity that is being accessed does not exist */
	SCMI_NOT_FOUND = -4,
	/** Requested settings are outside the legal range */
	SCMI_OUT_OF_RANGE = -5,
	/** The platform is out of resources and thus unable to process a command */
	SCMI_BUSY = -6,
	/** The message could not be correctly transmitted */
	SCMI_COMMS_ERROR = -7,
	/** Unspecified fault within the platform */
	SCMI_GENERIC_ERROR = -8,
	/** Hardware error in a platform component */
	SCMI_HARDWARE_ERROR = -9,
	/** The caller has violated the protocol specification */
	SCMI_PROTOCOL_ERROR = -10,
	/** The resource is currently in use and cannot be operated upon */
	SCMI_IN_USE = -11,
};

/**
 * @brief SCMI common command
 */
enum scmi_common_cmd {
	/** Returns the version of the protocol */
	SCMI_MSG_PROTOCOL_VERSION = 0x0,
	/** Returns the attributes of the protocol */
	SCMI_MSG_PROTOCOL_ATTRIBUTES = 0x1,
	/** Returns the attributes of a specific message */
	SCMI_MSG_MESSAGE_ATTRIBUTES = 0x2,
	/** Negotiates the protocol version with the platform */
	SCMI_MSG_NEGOTIATE_PROTOCOL_VERSION = 0x10,
};

/**
 * @struct scmi_protocol
 *
 * @brief SCMI protocol structure
 */
struct scmi_protocol {
	/** protocol ID */
	uint32_t id;
	/** TX channel */
	struct scmi_channel *tx;
	/** transport layer device */
	const struct device *transport;
	/** protocol private data */
	void *data;
	/** protocol supported version */
	uint32_t version;
};

/**
 * @struct scmi_message
 *
 * @brief SCMI message structure
 */
struct scmi_message {
	/** Message header */
	uint32_t hdr;
	/** Length of the message */
	uint32_t len;
	/** Pointer to the message payload */
	void *content;
};

/**
 * @brief Convert an SCMI status code to its Linux equivalent (if possible)
 *
 * @param scmi_status SCMI status code as shown in `enum scmi_status_code`
 *
 * @retval Linux equivalent status code
 */
int scmi_status_to_errno(int scmi_status);

/**
 * @brief Send an SCMI message and wait for its reply
 *
 * Blocking function used to send an SCMI message over
 * a given channel and wait for its reply
 *
 * @param proto pointer to SCMI protocol
 * @param msg pointer to SCMI message to send
 * @param reply pointer to SCMI message in which the reply is to be
 * written
 *
 * @retval 0 if successful
 * @retval negative errno if failure
 * @param use_polling Specifies the communication mechanism used by the scmi
 * platform to interact with agents.
 * - true: Polling mode — the platform actively checks the message status
 *   to determine if it has been processed
 * - false: Interrupt mode — the platform relies on SCMI interrupts to
 *   detect when a message has been handled.
 */
int scmi_send_message(struct scmi_protocol *proto,
		      struct scmi_message *msg, struct scmi_message *reply,
		      bool use_polling);

/**
 * @brief Get protocol version
 *
 * @param proto Protocol instance
 * @param version Pointer to store protocol version
 *
 * @retval 0 if successful
 * @retval negative errno if failure
 */
int scmi_protocol_get_version(struct scmi_protocol *proto, uint32_t *version);

/**
 * @brief Get protocol attributes
 *
 * @param proto Protocol instance
 * @param attributes Pointer to store protocol attributes
 *
 * @retval 0 if successful
 * @retval negative errno if failure
 */
int scmi_protocol_attributes_get(struct scmi_protocol *proto, uint32_t *attributes);

/**
 * @brief Get protocol message attributes
 *
 * @param proto Protocol instance
 * @param message_id Message ID to query
 * @param attributes Pointer to store message attributes
 *
 * @retval 0 if successful
 * @retval negative errno if failure
 */
int scmi_protocol_message_attributes_get(struct scmi_protocol *proto,
					uint32_t message_id, uint32_t *attributes);

/**
 * @brief Negotiate protocol version
 *
 * @param proto Protocol instance
 * @param version Desired protocol version
 *
 * @retval 0 if successful
 * @retval negative errno if failure
 */
int scmi_protocol_version_negotiate(struct scmi_protocol *proto, uint32_t version);

/**
 * @}
 */

/**
 * @brief Standard SCMI Protocol definitions
 * @defgroup scmi_protocols Protocols
 * @ingroup scmi_interface
 */

#endif /* _INCLUDE_ZEPHYR_DRIVERS_FIRMWARE_SCMI_PROTOCOL_H_ */
