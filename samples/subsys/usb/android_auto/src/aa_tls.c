/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_tls.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#ifdef CONFIG_MBEDTLS_DEBUG
#include <mbedtls/debug.h>
#include <zephyr_mbedtls_priv.h>
#endif

#include "aa_frame.h"

LOG_MODULE_REGISTER(aa_tls, CONFIG_SAMPLE_AA_LOG_LEVEL);

/* PEM credentials, NUL terminated as required by the Mbed TLS PEM parser */
static const unsigned char aa_cert_pem[] = {
#include "aa_cert.pem.inc"
	0x00
};
static const unsigned char aa_key_pem[] = {
#include "aa_key.pem.inc"
	0x00
};

static mbedtls_ssl_context ssl;
static mbedtls_ssl_config conf;
static mbedtls_x509_crt crt;
static mbedtls_pk_context pk;
static bool handshake_done;

static struct {
	const uint8_t *data;
	size_t len;
	size_t off;
} rx_io;

static void log_mbedtls_error(const char *what, int ret)
{
#ifdef MBEDTLS_ERROR_C
	char msg[64];

	mbedtls_strerror(ret, msg, sizeof(msg));
	LOG_ERR("%s: -0x%04x (%s)", what, (unsigned int)-ret, msg);
#else
	LOG_ERR("%s: -0x%04x", what, (unsigned int)-ret);
#endif
}

static int tls_send(void *ctx, const unsigned char *buf, size_t len)
{
	ARG_UNUSED(ctx);

	if (aa_frame_tx_record(buf, len) != 0) {
		return MBEDTLS_ERR_NET_SEND_FAILED;
	}

	return (int)len;
}

static int tls_recv(void *ctx, unsigned char *buf, size_t len)
{
	size_t avail = rx_io.len - rx_io.off;
	size_t n;

	ARG_UNUSED(ctx);

	if (avail == 0U) {
		return MBEDTLS_ERR_SSL_WANT_READ;
	}

	n = MIN(len, avail);
	memcpy(buf, rx_io.data + rx_io.off, n);
	rx_io.off += n;

	return (int)n;
}

int aa_tls_init(void)
{
	int ret;

	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	mbedtls_x509_crt_init(&crt);
	mbedtls_pk_init(&pk);

	ret = mbedtls_x509_crt_parse(&crt, aa_cert_pem, sizeof(aa_cert_pem));
	if (ret != 0) {
		log_mbedtls_error("Certificate parsing failed", ret);
		return -EIO;
	}

	ret = mbedtls_pk_parse_key(&pk, aa_key_pem, sizeof(aa_key_pem), NULL, 0);
	if (ret != 0) {
		log_mbedtls_error("Private key parsing failed", ret);
		return -EIO;
	}

	ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER,
					  MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
	if (ret != 0) {
		log_mbedtls_error("SSL config failed", ret);
		return -EIO;
	}

	mbedtls_ssl_conf_min_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
	mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
	/* The head unit certificate is not verified, the phone role only proves itself */
	mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);

	ret = mbedtls_ssl_conf_own_cert(&conf, &crt, &pk);
	if (ret != 0) {
		log_mbedtls_error("Own certificate setup failed", ret);
		return -EIO;
	}

#ifdef CONFIG_MBEDTLS_DEBUG
	mbedtls_ssl_conf_dbg(&conf, zephyr_mbedtls_debug, NULL);
	mbedtls_debug_set_threshold(CONFIG_SAMPLE_AA_TLS_DEBUG_LEVEL);
#endif

	ret = mbedtls_ssl_setup(&ssl, &conf);
	if (ret != 0) {
		log_mbedtls_error("SSL setup failed", ret);
		return -EIO;
	}

	mbedtls_ssl_set_bio(&ssl, NULL, tls_send, tls_recv, NULL);

	return 0;
}

int aa_tls_reset(void)
{
	int ret;

	aa_frame_lock();
	handshake_done = false;
	rx_io.data = NULL;
	rx_io.len = 0U;
	rx_io.off = 0U;
	ret = mbedtls_ssl_session_reset(&ssl);
	aa_frame_unlock();

	if (ret != 0) {
		log_mbedtls_error("Session reset failed", ret);
		return -EIO;
	}

	return 0;
}

int aa_tls_handshake_input(const uint8_t *data, size_t len)
{
	int ret;

	aa_frame_lock();
	rx_io.data = data;
	rx_io.len = len;
	rx_io.off = 0U;

	do {
		ret = mbedtls_ssl_handshake(&ssl);
	} while (ret == MBEDTLS_ERR_SSL_WANT_READ && rx_io.off < rx_io.len);

	rx_io.data = NULL;
	rx_io.len = 0U;
	rx_io.off = 0U;
	if (ret == 0) {
		handshake_done = true;
	}
	aa_frame_unlock();

	if (ret == 0) {
		LOG_INF("TLS handshake done: %s, %s", mbedtls_ssl_get_version(&ssl),
			mbedtls_ssl_get_ciphersuite(&ssl));
		return 0;
	}

	if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
		return -EAGAIN;
	}

	log_mbedtls_error("Handshake failed", ret);

	return -EIO;
}

bool aa_tls_handshake_done(void)
{
	return handshake_done;
}

int aa_tls_decrypt(uint8_t *buf, size_t len, size_t cap)
{
	size_t out = 0;
	int ret = 0;

	aa_frame_lock();
	rx_io.data = buf;
	rx_io.len = len;
	rx_io.off = 0U;

	while (rx_io.off < rx_io.len || mbedtls_ssl_get_bytes_avail(&ssl) > 0U) {
		int n = mbedtls_ssl_read(&ssl, buf + out, cap - out);

		if (n > 0) {
			out += (size_t)n;
			continue;
		}

		if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
			ret = -ECONNRESET;
		} else if (n == MBEDTLS_ERR_SSL_WANT_READ) {
			LOG_ERR("Truncated TLS record");
			ret = -EIO;
		} else {
			log_mbedtls_error("Decryption failed", n);
			ret = -EIO;
		}
		break;
	}

	rx_io.data = NULL;
	rx_io.len = 0U;
	rx_io.off = 0U;
	aa_frame_unlock();

	return (ret != 0) ? ret : (int)out;
}

int aa_tls_write(const uint8_t *data, size_t len)
{
	size_t off = 0;

	while (off < len) {
		int n = mbedtls_ssl_write(&ssl, data + off, len - off);

		if (n < 0) {
			log_mbedtls_error("Encryption failed", n);
			return -EIO;
		}
		off += (size_t)n;
	}

	return 0;
}

size_t aa_tls_max_record_payload(void)
{
	int n = mbedtls_ssl_get_max_out_record_payload(&ssl);

	return (n > 0) ? (size_t)n : 0U;
}
