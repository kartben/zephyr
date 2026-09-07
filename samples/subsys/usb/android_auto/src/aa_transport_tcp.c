/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_transport.h"

#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(aa_tcp, CONFIG_SAMPLE_AA_LOG_LEVEL);

/*
 * TCP transport: listens on CONFIG_SAMPLE_AA_TCP_PORT, the port the Desktop
 * Head Unit connects to by default, and serves one head unit at a time.
 */

static int listen_sock = -1;
static int client_sock = -1;

static int timeout_to_ms(k_timeout_t timeout)
{
	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
		return -1;
	}

	return (int)k_ticks_to_ms_floor32((uint32_t)timeout.ticks);
}

static int tcp_open(void)
{
	struct net_sockaddr_in addr = {
		.sin_family = NET_AF_INET,
		.sin_port = net_htons(CONFIG_SAMPLE_AA_TCP_PORT),
		.sin_addr = NET_INADDR_ANY_INIT,
	};
	int one = 1;
	int ret;

	listen_sock = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
	if (listen_sock < 0) {
		LOG_ERR("socket() failed (%d)", errno);
		return -errno;
	}

	(void)zsock_setsockopt(listen_sock, ZSOCK_SOL_SOCKET, ZSOCK_SO_REUSEADDR, &one,
			       sizeof(one));

	ret = zsock_bind(listen_sock, (struct net_sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		LOG_ERR("bind() failed (%d)", errno);
		return -errno;
	}

	ret = zsock_listen(listen_sock, 1);
	if (ret < 0) {
		LOG_ERR("listen() failed (%d)", errno);
		return -errno;
	}

	if (CONFIG_SAMPLE_AA_TCP_PORT == 0) {
		net_socklen_t addr_len = sizeof(addr);

		/* An ephemeral port was assigned, report it */
		(void)zsock_getsockname(listen_sock, (struct net_sockaddr *)&addr, &addr_len);
	}

	LOG_INF("Listening on TCP port %u", net_ntohs(addr.sin_port));

	return 0;
}

static int tcp_wait_link(k_timeout_t timeout)
{
	struct zsock_pollfd pfd = {.fd = listen_sock, .events = ZSOCK_POLLIN};
	struct net_sockaddr_in peer;
	net_socklen_t peer_len = sizeof(peer);
	int ret;

	ret = zsock_poll(&pfd, 1, timeout_to_ms(timeout));
	if (ret == 0) {
		return -ETIMEDOUT;
	}
	if (ret < 0) {
		return -errno;
	}

	client_sock = zsock_accept(listen_sock, (struct net_sockaddr *)&peer, &peer_len);
	if (client_sock < 0) {
		LOG_ERR("accept() failed (%d)", errno);
		return -errno;
	}

	LOG_INF("Head unit connected");

	return 0;
}

static int tcp_read(uint8_t *buf, size_t len, k_timeout_t timeout)
{
	struct zsock_pollfd pfd = {.fd = client_sock, .events = ZSOCK_POLLIN};
	ssize_t n;
	int ret;

	if (client_sock < 0) {
		return -ENOTCONN;
	}

	ret = zsock_poll(&pfd, 1, timeout_to_ms(timeout));
	if (ret == 0) {
		return -ETIMEDOUT;
	}
	if (ret < 0) {
		return -ENOTCONN;
	}

	n = zsock_recv(client_sock, buf, len, 0);
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

	if (client_sock < 0) {
		return -ENOTCONN;
	}

	while (off < len) {
		ssize_t n = zsock_send(client_sock, buf + off, len - off, 0);

		if (n <= 0) {
			return -ENOTCONN;
		}
		off += (size_t)n;
	}

	return 0;
}

static void tcp_close(void)
{
	if (client_sock >= 0) {
		(void)zsock_close(client_sock);
		client_sock = -1;
		LOG_INF("Head unit disconnected");
	}
}

static bool tcp_is_up(void)
{
	return client_sock >= 0;
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
