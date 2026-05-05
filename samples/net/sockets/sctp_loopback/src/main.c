/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>

#define SERVER_PORT 4242
#define CLIENT_MESSAGE "hello from the SCTP client"
#define SERVER_MESSAGE "hello from the SCTP server"

static int prepare_loopback_addr(struct net_sockaddr_in *addr)
{
	memset(addr, 0, sizeof(*addr));
	addr->sin_family = NET_AF_INET;
	addr->sin_port = net_htons(SERVER_PORT);

	if (zsock_inet_pton(NET_AF_INET, "127.0.0.1", &addr->sin_addr) != 1) {
		printk("Failed to parse loopback address\n");
		return -EINVAL;
	}

	return 0;
}

static void close_socket(int *sock)
{
	if (*sock >= 0) {
		(void)zsock_close(*sock);
		*sock = -1;
	}
}

int main(void)
{
	struct net_sockaddr_in loopback_addr;
	struct net_sockaddr_in peer_addr;
	net_socklen_t peer_addr_len = sizeof(peer_addr);
	char recv_buf[sizeof(SERVER_MESSAGE)];
	int listen_sock = -1;
	int client_sock = -1;
	int server_sock = -1;
	int ret;

	ret = prepare_loopback_addr(&loopback_addr);
	if (ret < 0) {
		return 0;
	}

	listen_sock = zsock_socket(NET_AF_INET, NET_SOCK_SEQPACKET, NET_IPPROTO_SCTP);
	if (listen_sock < 0) {
		printk("SCTP socket creation failed: %d\n", errno);
		printk("Select an offloaded socket backend that supports SCTP.\n");
		return 0;
	}

	ret = zsock_bind(listen_sock, (struct net_sockaddr *)&loopback_addr,
			 sizeof(loopback_addr));
	if (ret < 0) {
		printk("bind() failed: %d\n", errno);
		goto out;
	}

	ret = zsock_listen(listen_sock, 1);
	if (ret < 0) {
		printk("listen() failed: %d\n", errno);
		goto out;
	}

	client_sock = zsock_socket(NET_AF_INET, NET_SOCK_SEQPACKET, NET_IPPROTO_SCTP);
	if (client_sock < 0) {
		printk("client socket creation failed: %d\n", errno);
		goto out;
	}

	ret = zsock_connect(client_sock, (struct net_sockaddr *)&loopback_addr,
			    sizeof(loopback_addr));
	if (ret < 0) {
		printk("connect() failed: %d\n", errno);
		goto out;
	}

	server_sock = zsock_accept(listen_sock, (struct net_sockaddr *)&peer_addr,
				   &peer_addr_len);
	if (server_sock < 0) {
		printk("accept() failed: %d\n", errno);
		goto out;
	}

	ret = zsock_send(client_sock, CLIENT_MESSAGE, sizeof(CLIENT_MESSAGE), 0);
	if (ret != sizeof(CLIENT_MESSAGE)) {
		printk("client send failed: %d\n", errno);
		goto out;
	}

	ret = zsock_recv(server_sock, recv_buf, sizeof(recv_buf), 0);
	if (ret != sizeof(CLIENT_MESSAGE) ||
	    strcmp(recv_buf, CLIENT_MESSAGE) != 0) {
		printk("server recv failed: %d\n", errno);
		goto out;
	}

	ret = zsock_send(server_sock, SERVER_MESSAGE, sizeof(SERVER_MESSAGE), 0);
	if (ret != sizeof(SERVER_MESSAGE)) {
		printk("server send failed: %d\n", errno);
		goto out;
	}

	memset(recv_buf, 0, sizeof(recv_buf));
	ret = zsock_recv(client_sock, recv_buf, sizeof(recv_buf), 0);
	if (ret != sizeof(SERVER_MESSAGE) ||
	    strcmp(recv_buf, SERVER_MESSAGE) != 0) {
		printk("client recv failed: %d\n", errno);
		goto out;
	}

	printk("SCTP loopback exchange completed on port %d\n", SERVER_PORT);

out:
	close_socket(&server_sock);
	close_socket(&client_sock);
	close_socket(&listen_sock);

	return 0;
}
