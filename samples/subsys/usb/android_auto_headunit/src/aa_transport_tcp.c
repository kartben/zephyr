/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_transport.h"

#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(aa_tcp, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * TCP transport: the head unit connects to a phone that exposes the projection
 * protocol on a TCP port, which is how the companion accessory sample runs on
 * native_sim.
 */

static int sock = -1;

static int timeout_to_ms(k_timeout_t timeout)
{
	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
		return -1;
	}

	return (int)k_ticks_to_ms_floor32((uint32_t)timeout.ticks);
}

static int tcp_open(void)
{
	LOG_INF("Head unit connecting to %s:%u", CONFIG_SAMPLE_AA_HU_TCP_HOST,
		CONFIG_SAMPLE_AA_HU_TCP_PORT);

	return 0;
}

static int tcp_wait_link(k_timeout_t timeout)
{
	struct net_sockaddr_in addr = {
		.sin_family = NET_AF_INET,
		.sin_port = net_htons(CONFIG_SAMPLE_AA_HU_TCP_PORT),
	};
	int ret;

	ARG_UNUSED(timeout);

	ret = net_addr_pton(NET_AF_INET, CONFIG_SAMPLE_AA_HU_TCP_HOST, &addr.sin_addr);
	if (ret < 0) {
		LOG_ERR("Bad host address %s", CONFIG_SAMPLE_AA_HU_TCP_HOST);
		return -EINVAL;
	}

	sock = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
	if (sock < 0) {
		return -errno;
	}

	ret = zsock_connect(sock, (struct net_sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		(void)zsock_close(sock);
		sock = -1;
		return -ETIMEDOUT;
	}

	LOG_INF("Connected to the phone");

	return 0;
}

static int tcp_read(uint8_t *buf, size_t len, k_timeout_t timeout)
{
	struct zsock_pollfd pfd = {.fd = sock, .events = ZSOCK_POLLIN};
	ssize_t n;
	int ret;

	if (sock < 0) {
		return -ENOTCONN;
	}

	ret = zsock_poll(&pfd, 1, timeout_to_ms(timeout));
	if (ret == 0) {
		return -ETIMEDOUT;
	}
	if (ret < 0) {
		return -ENOTCONN;
	}

	n = zsock_recv(sock, buf, len, 0);
	if (n == 0) {
		return -ENOTCONN;
	}
	if (n < 0) {
		return (errno == EAGAIN) ? -ETIMEDOUT : -ENOTCONN;
	}

	return (int)n;
}

static int tcp_write(const uint8_t *buf, size_t len)
{
	size_t off = 0;

	if (sock < 0) {
		return -ENOTCONN;
	}

	while (off < len) {
		ssize_t n = zsock_send(sock, buf + off, len - off, 0);

		if (n <= 0) {
			return -ENOTCONN;
		}
		off += (size_t)n;
	}

	return 0;
}

static void tcp_close(void)
{
	if (sock >= 0) {
		(void)zsock_close(sock);
		sock = -1;
		LOG_INF("Disconnected from the phone");
	}
}

static bool tcp_is_up(void)
{
	return sock >= 0;
}

static const struct aa_transport tcp_transport = {
	.open = tcp_open,
	.wait_link = tcp_wait_link,
	.read = tcp_read,
	.write = tcp_write,
	.close = tcp_close,
	.is_up = tcp_is_up,
	.name = "tcp",
};

const struct aa_transport *aa_transport_get(void)
{
	return &tcp_transport;
}
