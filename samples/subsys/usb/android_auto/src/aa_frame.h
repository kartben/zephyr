/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_FRAME_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_FRAME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

/*
 * Messenger: turns messages into frames on the transport byte stream and back.
 *
 * Frame layout: channel (u8), flags (u8), payload length (u16 BE), total
 * message length (u32 BE, only in the first fragment of a fragmented message),
 * payload. A message payload starts with the u16 BE message id. Once the
 * session is authenticated every payload is one TLS record.
 */

/** A frame received from the head unit; payload points into the RX buffer. */
struct aa_frame_rx {
	uint8_t channel;
	uint8_t flags;
	uint16_t len;
	uint32_t total;
	uint8_t *payload;
};

/** @brief Initialize the messenger. */
int aa_frame_init(void);

/** @brief Forget any partially sent message and switch encryption off. */
void aa_frame_reset(void);

/** @brief Enable encryption of every message from now on (after AUTH_COMPLETE). */
void aa_frame_set_encrypted(bool on);

/** @brief Serialize access to the TLS context, the TX buffers and the transport. */
void aa_frame_lock(void);
void aa_frame_unlock(void);

/**
 * @brief Send one TLS record. Called by the TLS layer with the lock held.
 *
 * Before the session is encrypted the record is a handshake record and goes
 * out as an SSL_HANDSHAKE control message; afterwards it is the payload of the
 * frame currently being assembled.
 */
int aa_frame_tx_record(const uint8_t *record, size_t len);

/**
 * @brief Send a small message in a single frame.
 *
 * @param channel Channel id.
 * @param control true to set the control message flag.
 * @param msg_id Message id.
 * @param body Message body (protobuf or raw), may be NULL when len is 0.
 * @param len Body length.
 *
 * @retval 0 Success.
 * @retval -EMSGSIZE The body does not fit one frame.
 * @return Negative errno from the transport.
 */
int aa_msg_send(uint8_t channel, bool control, uint16_t msg_id, const uint8_t *body, size_t len);

/**
 * @brief Start a message whose body is streamed in fragments.
 *
 * @param channel Channel id.
 * @param msg_id Message id.
 * @param body_len Exact body length that will be written.
 */
int aa_msg_begin(uint8_t channel, uint16_t msg_id, size_t body_len);

/** @brief Append body bytes to the message started with aa_msg_begin(). */
int aa_msg_write(const uint8_t *data, size_t len);

/** @brief Flush the last fragment of the streamed message. */
int aa_msg_end(void);

/** @brief Drop a streamed message after an error. */
void aa_msg_abort(void);

/**
 * @brief Receive one frame.
 *
 * @param frame Filled with the frame; the payload is valid until the next call.
 * @param timeout Timeout for the first byte, the rest of the frame waits forever.
 *
 * @retval 0 Success.
 * @retval -ETIMEDOUT No frame started within the timeout.
 * @retval -ENOTCONN The link is gone.
 * @retval -EMSGSIZE The frame is larger than the RX buffer.
 */
int aa_frame_recv(struct aa_frame_rx *frame, k_timeout_t timeout);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_FRAME_H_ */
