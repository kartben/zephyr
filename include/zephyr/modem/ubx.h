/*
 * Copyright (c) 2024 NXP
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief u-blox (UBX) modem module.
 *
 * The UBX modem module runs scripts made of UBX frames over a modem pipe and
 * dispatches solicited responses and unsolicited messages received from a
 * u-blox GNSS device.
 */

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/modem/pipe.h>
#include <zephyr/modem/ubx/protocol.h>

#ifndef ZEPHYR_MODEM_UBX_
#define ZEPHYR_MODEM_UBX_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Modem Ubx
 * @defgroup modem_ubx Modem Ubx
 * @since 3.7
 * @version 1.0.0
 * @ingroup modem
 * @{
 */

struct modem_ubx;

/**
 * @brief Callback invoked when a received frame matches a filter
 *
 * @param ubx Modem Ubx instance
 * @param frame Received UBX frame
 * @param len Length of the received frame in bytes
 * @param user_data Free to use user data set during modem_ubx_init()
 */
typedef void (*modem_ubx_match_callback)(struct modem_ubx *ubx,
					 const struct ubx_frame *frame,
					 size_t len,
					 void *user_data);

/** @brief Modem Ubx match */
struct modem_ubx_match {
	/** Filter used to select the frames handled by this match */
	struct ubx_frame_match filter;
	/** Handler invoked when a received frame matches the filter */
	modem_ubx_match_callback handler;
};

/**
 * @brief Define an array of @ref modem_ubx_match instances
 *
 * @param _name Name of the array variable to declare
 * @param ... @ref modem_ubx_match initializers populating the array
 */
#define MODEM_UBX_MATCH_ARRAY_DEFINE(_name, ...)						   \
	struct modem_ubx_match _name[] = {__VA_ARGS__};

/**
 * @brief Initializer for a @ref modem_ubx_match instance
 *
 * @param _class_id Message class to match (see @ref ubx_class_id)
 * @param _msg_id Message identifier to match
 * @param _handler Handler invoked when a frame matches
 */
#define MODEM_UBX_MATCH_DEFINE(_class_id, _msg_id, _handler)					   \
{												   \
	.filter = {										   \
		.class = _class_id,								   \
		.id = _msg_id,									   \
	},											   \
	.handler = _handler,									   \
}

/** @brief Modem Ubx script */
struct modem_ubx_script {
	/** UBX frame to transmit */
	struct {
		/** Buffer holding the UBX frame to transmit */
		const void *buf;
		/** Length of the frame to transmit in bytes */
		uint16_t len;
	} request;
	/** Response received from the device */
	struct {
		/** Buffer used to store the received response frame */
		uint8_t *buf;
		/** Size of the response buffer in bytes */
		uint16_t buf_len;
		/** Length of the received response in bytes */
		uint16_t received_len;
	} response;
	/** Match used to terminate the script when the expected frame is received */
	struct modem_ubx_match match;
	/** Number of times the request is retried before giving up */
	uint16_t retry_count;
	/** Total time allowed for the script to complete */
	k_timeout_t timeout;
};

/**
 * @brief Modem Ubx instance internal context
 * @warning Do not modify any members of this struct directly
 */
struct modem_ubx {
	/** @cond INTERNAL_HIDDEN */
	/* User data passed with match callbacks */
	void *user_data;
	/* Set when the instance is attached to a pipe */
	atomic_t attached;
	/* Receive buffer */
	uint8_t *receive_buf;
	uint16_t receive_buf_size;
	uint16_t receive_buf_offset;
	/* Script currently being executed */
	struct modem_ubx_script *script;
	/* Pipe used to send and receive data */
	struct modem_pipe *pipe;
	/* Process received data */
	struct k_work process_work;
	/* Script execution synchronization */
	struct k_sem script_stopped_sem;
	struct k_sem script_running_sem;
	/* Unsolicited message matches */
	struct {
		const struct modem_ubx_match *array;
		size_t size;
	} unsol_matches;
	/** @endcond */
};

/** @brief Modem Ubx configuration */
struct modem_ubx_config {
	/** Free to use user data passed with match callbacks */
	void *user_data;
	/** Receive buffer used to store received frames */
	uint8_t *receive_buf;
	/** Size of the receive buffer in bytes */
	uint16_t receive_buf_size;
	/** Array of unsolicited message matches */
	struct {
		/** Pointer to the array of unsolicited matches */
		const struct modem_ubx_match *array;
		/** Number of elements in the array */
		size_t size;
	} unsol_matches;
};

/**
 * @brief Attach pipe to Modem Ubx
 *
 * @param ubx Modem Ubx instance
 * @param pipe Pipe instance to attach Modem Ubx instance to
 * @returns 0 if successful
 * @returns negative errno code if failure
 * @note Modem Ubx instance is enabled if successful
 */
int modem_ubx_attach(struct modem_ubx *ubx, struct modem_pipe *pipe);

/**
 * @brief Release pipe from Modem Ubx instance
 *
 * @param ubx Modem Ubx instance
 */
void modem_ubx_release(struct modem_ubx *ubx);

/**
 * @brief Initialize Modem Ubx instance
 * @param ubx Modem Ubx instance
 * @param config Configuration which shall be applied to the Modem Ubx instance
 * @note Modem Ubx instance must be attached to a pipe instance
 */
int modem_ubx_init(struct modem_ubx *ubx, const struct modem_ubx_config *config);

/**
 * @brief Writes the ubx frame in script.request and reads back its response (if available)
 * @details For each ubx frame sent, the device responds in 0, 1 or both of the following ways:
 *	1. The device sends back a UBX-ACK frame to denote 'acknowledge' and 'not-acknowledge'.
 *		Note: the message id of UBX-ACK frame determines whether the device acknowledged.
 *		Ex: when we send a UBX-CFG frame, the device responds with a UBX-ACK frame.
 *	2. The device sends back the same frame that we sent to it, with it's payload populated.
 *		It's used to get the current configuration corresponding to the frame that we sent.
 *		Ex: frame types such as "get" or "poll" ubx frames respond this way.
 *		This response (if received) is written to script.response.
 *
 *	This function writes the ubx frame in script.request then reads back it's response.
 *	If script.match is not NULL, then every ubx frame received from the device is compared with
 *	script.match to check if a match occurred. This could be used to match UBX-ACK frame sent
 *	from the device by populating script.match with UBX-ACK that the script expects to receive.
 *
 *	The script terminates when either of the following happens:
 *		1. script.match is successfully received and matched.
 *		2. timeout (denoted by script.timeout) occurs.
 * @param ubx Modem Ubx instance
 * @param script Script to be executed
 * @note The length of ubx frame in the script.request should not exceed UBX_FRAME_SZ_MAX
 * @note Modem Ubx instance must be attached to a pipe instance
 * @returns 0 if successful
 * @returns negative errno code if failure
 */
int modem_ubx_run_script(struct modem_ubx *ubx, struct modem_ubx_script *script);

/**
 * @brief Run a script once for each frame in an array
 * @details Executes @p script repeatedly, using each frame in @p array as the
 *	script request in turn. Execution stops at the first frame that fails.
 * @param ubx Modem Ubx instance
 * @param script Script to be executed for each frame
 * @param array Array of UBX frames used as the script request
 * @param array_size Number of frames in @p array
 * @note Modem Ubx instance must be attached to a pipe instance
 * @returns 0 if successful
 * @returns negative errno code if failure
 */
int modem_ubx_run_script_for_each(struct modem_ubx *ubx, struct modem_ubx_script *script,
				  struct ubx_frame *array, size_t array_size);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MODEM_UBX_ */
