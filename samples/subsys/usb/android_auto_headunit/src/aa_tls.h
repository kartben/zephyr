/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_TLS_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_TLS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * TLS 1.2 client side of the Android Auto session. The head unit initiates the
 * handshake; its records travel inside SSL_HANDSHAKE control messages and,
 * once established, every encrypted frame payload is one application data
 * record.
 */

/** @brief Parse the credentials and set up the TLS client context. */
int aa_tls_init(void);

/** @brief Reset the session for a new link. */
int aa_tls_reset(void);

/**
 * @brief Start the handshake and flush the ClientHello as an SSL_HANDSHAKE message.
 *
 * @retval 0 The handshake completed (unlikely on the first call).
 * @retval -EAGAIN A server response is needed.
 * @retval -EIO The handshake failed.
 */
int aa_tls_handshake_start(void);

/**
 * @brief Feed the payload of one SSL_HANDSHAKE message and advance the handshake.
 *
 * @retval 0 The handshake completed.
 * @retval -EAGAIN More handshake messages are needed.
 * @retval -EIO The handshake failed.
 */
int aa_tls_handshake_input(const uint8_t *data, size_t len);

/** @brief True once the handshake completed. */
bool aa_tls_handshake_done(void);

/** @brief Decrypt the TLS record(s) held in buf in place; returns plaintext length. */
int aa_tls_decrypt(uint8_t *buf, size_t len, size_t cap);

/** @brief Encrypt and send plaintext; the messenger frames the record. Lock held. */
int aa_tls_write(const uint8_t *data, size_t len);

/** @brief Largest plaintext that fits one record. */
size_t aa_tls_max_record_payload(void);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_TLS_H_ */
