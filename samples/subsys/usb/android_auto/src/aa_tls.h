/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_TLS_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_TLS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * TLS 1.2 server side of the Android Auto session. Records never touch a
 * socket: handshake records travel inside SSL_HANDSHAKE control messages and,
 * once the session is established, every encrypted frame payload is one TLS
 * application data record.
 */

/**
 * @brief Parse the credentials and set up the TLS server context.
 *
 * @retval 0 Success.
 * @retval -EIO Mbed TLS reported an error.
 */
int aa_tls_init(void);

/** @brief Reset the session for a new link. */
int aa_tls_reset(void);

/**
 * @brief Feed the payload of one SSL_HANDSHAKE message and advance the handshake.
 *
 * Records produced in response are sent as SSL_HANDSHAKE messages.
 *
 * @retval 0 The handshake completed.
 * @retval -EAGAIN More handshake messages are needed.
 * @retval -EIO The handshake failed.
 */
int aa_tls_handshake_input(const uint8_t *data, size_t len);

/** @brief True once the handshake completed. */
bool aa_tls_handshake_done(void);

/**
 * @brief Decrypt the TLS record(s) held in buf in place.
 *
 * @param buf Record bytes, overwritten with the plaintext.
 * @param len Number of record bytes.
 * @param cap Size of buf.
 *
 * @return Plaintext length.
 * @retval -ECONNRESET The peer closed the session.
 * @retval -EIO Decryption failed.
 */
int aa_tls_decrypt(uint8_t *buf, size_t len, size_t cap);

/**
 * @brief Encrypt and send plaintext; the messenger frames the resulting record.
 *
 * Must be called with the messenger lock held.
 *
 * @retval 0 Success.
 * @retval -EIO Mbed TLS or transport error.
 */
int aa_tls_write(const uint8_t *data, size_t len);

/** @brief Largest plaintext that fits one record. */
size_t aa_tls_max_record_payload(void);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_TLS_H_ */
