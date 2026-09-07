/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_SESSION_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_SESSION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pb.h>

enum aa_state {
	AA_STATE_LINK_DOWN,
	AA_STATE_WAIT_VERSION,
	AA_STATE_HANDSHAKE,
	AA_STATE_WAIT_AUTH,
	AA_STATE_WAIT_SERVICE_DISCOVERY,
	AA_STATE_OPENING_CHANNELS,
	AA_STATE_VIDEO_SETUP,
	AA_STATE_RUNNING,
	AA_STATE_SHUTDOWN,
};

struct aa_session {
	enum aa_state state;
	uint16_t hu_major;
	uint16_t hu_minor;
	bool has_video;
	bool has_input;
	bool has_sensor;
	uint8_t video_ch;
	uint8_t input_ch;
	uint8_t sensor_ch;
	uint32_t video_config_idx;
	uint16_t touch_width;
	uint16_t touch_height;
	uint8_t open_list[3];
	uint8_t open_count;
	uint8_t open_idx;
};

/** @brief Session state shared by the channel handlers (RX thread context). */
struct aa_session *aa_session_get(void);

/**
 * @brief Initialize the protocol modules and start the receive thread.
 *
 * @retval 0 Success.
 * @return Negative errno on failure.
 */
int aa_session_start(void);

/** @brief Move to a new state. */
void aa_session_set_state(enum aa_state state);

/** @brief Tear the link down after an unrecoverable error. */
void aa_session_abort(const char *reason);

/** @brief Forget the current session and wait for a new version request on the same link. */
void aa_session_restart(void);

/** @brief Open the channels found by service discovery, one after the other. */
int aa_session_open_channels(void);

/**
 * @brief Encode a protobuf message into a buffer.
 *
 * @return Encoded length, or -EMSGSIZE when it does not fit.
 */
int aa_pb_encode(uint8_t *buf, size_t size, const pb_msgdesc_t *fields, const void *msg);

/**
 * @brief Decode a protobuf message.
 *
 * @retval 0 Success.
 * @retval -EBADMSG Decoding failed.
 */
int aa_pb_decode(const uint8_t *buf, size_t len, const pb_msgdesc_t *fields, void *msg);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_SESSION_H_ */
