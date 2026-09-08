/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SESSION_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SESSION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pb.h>

enum aa_hu_state {
	AA_HU_LINK_DOWN,
	AA_HU_WAIT_VERSION,
	AA_HU_HANDSHAKE,
	AA_HU_WAIT_SERVICE_DISCOVERY,
	AA_HU_RUNNING,
	AA_HU_SHUTDOWN,
};

struct aa_hu_session {
	enum aa_hu_state state;
	uint16_t md_major;
	uint16_t md_minor;
	uint8_t video_ch;
	uint8_t input_ch;
	uint8_t sensor_ch;
	uint8_t audio_ch[3];
	int32_t video_session;
	bool video_focused;
};

/** @brief Head unit session shared by the channel handlers. */
struct aa_hu_session *aa_hu_session_get(void);

/** @brief Initialize the protocol modules and start the receive thread. */
int aa_hu_session_start(void);

/** @brief Move to a new state. */
void aa_hu_session_set_state(enum aa_hu_state state);

/** @brief Tear the link down. */
void aa_hu_session_abort(const char *reason);

/** @brief Encode a protobuf message into a buffer. */
int aa_pb_encode(uint8_t *buf, size_t size, const pb_msgdesc_t *fields, const void *msg);

/** @brief Decode a protobuf message. */
int aa_pb_decode(const uint8_t *buf, size_t len, const pb_msgdesc_t *fields, void *msg);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SESSION_H_ */
