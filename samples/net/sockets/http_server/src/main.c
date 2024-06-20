/*
 * Copyright (c) 2023, Emna Rekik
 * Copyright (c) 2024, Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_http_server_sample, LOG_LEVEL_DBG);

static uint8_t index_html_gz[] = {
#include "index.html.gz.inc"
};

#if defined(CONFIG_NET_SAMPLE_HTTP_SERVICE)
static uint16_t test_http_service_port = CONFIG_NET_SAMPLE_HTTP_SERVER_SERVICE_PORT;
HTTP_SERVICE_DEFINE(test_http_service, "192.168.1.180", &test_http_service_port, 1, 10, NULL);

struct http_resource_detail_static index_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = index_html_gz,
	.static_data_len = sizeof(index_html_gz),
};

HTTP_RESOURCE_DEFINE(index_html_gz_resource, test_http_service, "/",
		     &index_html_gz_resource_detail);

static uint8_t recv_buffer[1024];

static int dyn_handler(struct http_client_ctx *client, enum http_data_status status,
		       uint8_t *buffer, size_t len, void *user_data)
{
#define MAX_TEMP_PRINT_LEN 32
	static char print_str[MAX_TEMP_PRINT_LEN];
	enum http_method method = client->method;
	static size_t processed;

	__ASSERT_NO_MSG(buffer != NULL);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		LOG_DBG("Transaction aborted after %zd bytes.", processed);
		processed = 0;
		return 0;
	}

	processed += len;

	snprintf(print_str, sizeof(print_str), "%s received (%zd bytes)",
		 http_method_str(method), len);
	LOG_HEXDUMP_DBG(buffer, len, print_str);

	if (status == HTTP_SERVER_DATA_FINAL) {
		LOG_DBG("All data received (%zd bytes).", processed);
		processed = 0;
	}

	/* This will echo data back to client as the buffer and recv_buffer
	 * point to same area.
	 */
	return len;
}

struct http_resource_detail_dynamic dyn_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods =
			BIT(HTTP_GET) | BIT(HTTP_POST),
	},
	.cb = dyn_handler,
	.data_buffer = recv_buffer,
	.data_buffer_len = sizeof(recv_buffer),
	.user_data = NULL,
};

HTTP_RESOURCE_DEFINE(dyn_resource, test_http_service, "/dynamic",
		     &dyn_resource_detail);

#if defined(CONFIG_NET_SAMPLE_WEBSOCKET_SERVICE)
extern int ws_setup(int ws_socket, void *user_data);

static uint8_t ws_recv_buffer[1024];

struct http_resource_detail_websocket ws_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_WEBSOCKET,

			/* We need HTTP/1.1 Get method for upgrading */
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		},
	.cb = ws_setup,
	.data_buffer = ws_recv_buffer,
	.data_buffer_len = sizeof(ws_recv_buffer),
	.user_data = NULL, /* Fill this for any user specific data */
};

HTTP_RESOURCE_DEFINE(ws_resource, test_http_service, "/", &ws_resource_detail);

#endif /* CONFIG_NET_SAMPLE_WEBSOCKET_SERVICE */
#endif /* CONFIG_NET_SAMPLE_HTTP_SERVICE */

#if defined(CONFIG_NET_SAMPLE_HTTPS_SERVICE)
#include "certificate.h"

static const sec_tag_t sec_tag_list_verify_none[] = {
		HTTP_SERVER_CERTIFICATE_TAG,
#if defined(CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED)
		PSK_TAG,
#endif
	};

static uint16_t test_https_service_port = CONFIG_NET_SAMPLE_HTTPS_SERVER_SERVICE_PORT;
HTTPS_SERVICE_DEFINE(test_https_service, CONFIG_NET_CONFIG_MY_IPV4_ADDR,
		     &test_https_service_port, 1, 10, NULL,
		     sec_tag_list_verify_none, sizeof(sec_tag_list_verify_none));

static struct http_resource_detail_static index_html_gz_resource_detail_https = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
		},
	.static_data = index_html_gz,
	.static_data_len = sizeof(index_html_gz),
};

HTTP_RESOURCE_DEFINE(index_html_gz_resource_https, test_https_service, "/",
		     &index_html_gz_resource_detail_https);

#endif /* CONFIG_NET_SAMPLE_HTTPS_SERVICE */

static void setup_tls(void)
{
#if defined(CONFIG_NET_SAMPLE_HTTPS_SERVICE)
#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
	int err;

#if defined(CONFIG_NET_SAMPLE_CERTS_WITH_SC)
	err = tls_credential_add(HTTP_SERVER_CERTIFICATE_TAG,
				 TLS_CREDENTIAL_CA_CERTIFICATE,
				 ca_certificate,
				 sizeof(ca_certificate));
	if (err < 0) {
		LOG_ERR("Failed to register CA certificate: %d", err);
	}
#endif /* defined(CONFIG_NET_SAMPLE_CERTS_WITH_SC) */

	err = tls_credential_add(HTTP_SERVER_CERTIFICATE_TAG,
				 TLS_CREDENTIAL_SERVER_CERTIFICATE,
				 server_certificate,
				 sizeof(server_certificate));
	if (err < 0) {
		LOG_ERR("Failed to register public certificate: %d", err);
	}

	err = tls_credential_add(HTTP_SERVER_CERTIFICATE_TAG,
				 TLS_CREDENTIAL_PRIVATE_KEY,
				 private_key, sizeof(private_key));
	if (err < 0) {
		LOG_ERR("Failed to register private key: %d", err);
	}

#if defined(CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED)
	err = tls_credential_add(PSK_TAG,
				 TLS_CREDENTIAL_PSK,
				 psk,
				 sizeof(psk));
	if (err < 0) {
		LOG_ERR("Failed to register PSK: %d", err);
	}

	err = tls_credential_add(PSK_TAG,
				 TLS_CREDENTIAL_PSK_ID,
				 psk_id,
				 sizeof(psk_id) - 1);
	if (err < 0) {
		LOG_ERR("Failed to register PSK ID: %d", err);
	}
#endif /* defined(CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED) */
#endif /* defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS) */
#endif /* defined(CONFIG_NET_SAMPLE_HTTPS_SERVICE) */
}

#include <zephyr/net/wifi_mgmt.h>

void wifi_connect(void)
{
	int ret;
#if defined(CONFIG_WIFI)
	int nr_tries = 10;
	struct net_if *iface = net_if_get_default();
	static struct wifi_connect_req_params cnx_params = {
		.ssid = "MY_SSID",
		.ssid_length = 0,
		.psk = "MY_PWD",
		.psk_length = 0,
		.channel = 0,
		.security = WIFI_SECURITY_TYPE_PSK,
	};

	cnx_params.ssid_length = strlen(cnx_params.ssid);
	cnx_params.psk_length = strlen(cnx_params.psk);

	/* Let's wait few seconds to allow wifi device be on-line */

	LOG_INF("Connecting to WiFi network %s...", cnx_params.ssid);

	while (nr_tries-- > 0) {
		ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params,
			       sizeof(struct wifi_connect_req_params));
		if (ret == 0) {
			break;
		}

		// LOG_INF("Connect request failed %d. Waiting iface be up...", ret);
		k_msleep(500);
	}
#endif
}

int main(void)
{
	wifi_connect();

	/* hack :) would be better to properly wait for an IP to be assigned */
	k_sleep(K_SECONDS(10));

	setup_tls();
	http_server_start();
	return 0;
}
