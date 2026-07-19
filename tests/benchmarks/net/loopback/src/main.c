/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Loopback network throughput benchmark for native_sim.
 *
 * Measures host wall-clock time (native simulator pseudo-host real time
 * clock), which on native_sim tracks the real CPU cost of the stack,
 * independent of simulated time.
 *
 * Benchmarks:
 *  - UDP ping-pong (per-packet stack cost, several payload sizes)
 *  - UDP one-way blast with app-level batching (RX path throughput)
 *  - TCP bulk transfer (segmentation, windowing, ACK processing)
 */


#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <string.h>
#include <stdio.h>

#include "native_rtc.h"
#include "nsi_main.h"
#include "nsi_host_trampolines.h"

static int scale_div = 1;

static int scaled(int n)
{
	return n / scale_div;
}

#define UDP_PORT_A 4242
#define UDP_PORT_B 4243
#define TCP_PORT   4244

static uint8_t txbuf[2048];
static uint8_t rxbuf[2048];

K_THREAD_STACK_DEFINE(peer_stack, 16384);
static struct k_thread peer_thread;

static uint64_t now_us(void)
{
	return native_rtc_gettime_us(RTC_CLOCK_PSEUDOHOSTREALTIME);
}

static int udp_socket_bound(uint16_t port)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
	};
	int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock < 0) {
		printf("socket fail %d\n", -errno);
		nsi_exit(1);
	}
	zsock_inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
	if (zsock_bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("bind fail %d\n", -errno);
		nsi_exit(1);
	}
	return sock;
}

static void udp_connect_to(int sock, uint16_t port)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
	};

	zsock_inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
	if (zsock_connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("connect fail %d\n", -errno);
		nsi_exit(1);
	}
}

/* ------------- UDP ping-pong ------------- */

struct pong_arg {
	int sock;
	int rounds;
	int size;
};

static void pong_fn(void *a, void *b, void *c)
{
	struct pong_arg *arg = a;
	static uint8_t buf[2048];

	for (int i = 0; i < arg->rounds; i++) {
		int len = zsock_recv(arg->sock, buf, sizeof(buf), 0);

		if (len <= 0) {
			printf("pong recv fail %d\n", -errno);
			nsi_exit(1);
		}
		if (zsock_send(arg->sock, buf, len, 0) < 0) {
			printf("pong send fail %d\n", -errno);
			nsi_exit(1);
		}
	}
}

static void bench_udp_pingpong(int size, int rounds)
{
	int sa = udp_socket_bound(UDP_PORT_A);
	int sb = udp_socket_bound(UDP_PORT_B);
	struct pong_arg arg = { .sock = sb, .rounds = rounds, .size = size };

	udp_connect_to(sa, UDP_PORT_B);
	udp_connect_to(sb, UDP_PORT_A);

	k_thread_create(&peer_thread, peer_stack, K_THREAD_STACK_SIZEOF(peer_stack),
			pong_fn, &arg, NULL, NULL,
			K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

	uint64_t t0 = now_us();

	for (int i = 0; i < rounds; i++) {
		if (zsock_send(sa, txbuf, size, 0) < 0) {
			printf("ping send fail %d\n", -errno);
			nsi_exit(1);
		}
		int len = zsock_recv(sa, rxbuf, sizeof(rxbuf), 0);

		if (len != size) {
			printf("ping recv fail len=%d %d\n", len, -errno);
			nsi_exit(1);
		}
	}

	uint64_t dt = now_us() - t0;

	k_thread_join(&peer_thread, K_FOREVER);
	zsock_close(sa);
	zsock_close(sb);

	/* 2 packets per round */
	double pps = (double)rounds * 2.0 * 1e6 / (double)dt;
	double mbps = pps * size * 8.0 / 1e6;

	printf("RESULT udp_pingpong size=%-5d rounds=%d time_us=%llu pps=%.0f goodput_mbps=%.1f\n",
	       size, rounds, (unsigned long long)dt, pps, mbps);
}

/* ------------- UDP one-way batched blast ------------- */

struct blast_arg {
	int sock;
	int total;
	int size;
	int batch;
};

static void blast_rx_fn(void *a, void *b, void *c)
{
	struct blast_arg *arg = a;
	static uint8_t buf[2048];
	uint8_t ack = 1;
	int got = 0;

	while (got < arg->total) {
		int len = zsock_recv(arg->sock, buf, sizeof(buf), 0);

		if (len <= 0) {
			printf("blast recv fail %d\n", -errno);
			nsi_exit(1);
		}
		got++;
		if ((got % arg->batch) == 0) {
			zsock_send(arg->sock, &ack, 1, 0);
		}
	}
}

static void bench_udp_blast(int size, int total, int batch)
{
	int sa = udp_socket_bound(UDP_PORT_A);
	int sb = udp_socket_bound(UDP_PORT_B);
	struct blast_arg arg = { .sock = sb, .total = total, .size = size, .batch = batch };

	udp_connect_to(sa, UDP_PORT_B);
	udp_connect_to(sb, UDP_PORT_A);

	k_thread_create(&peer_thread, peer_stack, K_THREAD_STACK_SIZEOF(peer_stack),
			blast_rx_fn, &arg, NULL, NULL,
			K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

	uint64_t t0 = now_us();

	for (int i = 0; i < total; i++) {
		int ret = zsock_send(sa, txbuf, size, 0);

		if (ret < 0) {
			if (errno == ENOMEM || errno == EAGAIN) {
				k_yield();
				i--;
				continue;
			}
			printf("blast send fail %d\n", -errno);
			nsi_exit(1);
		}
		if (((i + 1) % batch) == 0) {
			uint8_t ack;

			zsock_recv(sa, &ack, 1, 0);
		}
	}

	uint64_t dt = now_us() - t0;

	k_thread_join(&peer_thread, K_FOREVER);
	zsock_close(sa);
	zsock_close(sb);

	double pps = (double)total * 1e6 / (double)dt;
	double mbps = pps * size * 8.0 / 1e6;

	printf("RESULT udp_blast    size=%-5d total=%d time_us=%llu pps=%.0f goodput_mbps=%.1f\n",
	       size, total, (unsigned long long)dt, pps, mbps);
}

/* ------------- TCP bulk ------------- */

struct tcp_arg {
	long long total_bytes;
};

static void tcp_rx_fn(void *a, void *b, void *c)
{
	struct tcp_arg *arg = a;
	static uint8_t buf[4096];
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(TCP_PORT),
	};
	int lsock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	zsock_inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
	if (zsock_bind(lsock, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    zsock_listen(lsock, 1) < 0) {
		printf("tcp listen fail %d\n", -errno);
		nsi_exit(1);
	}

	int conn = zsock_accept(lsock, NULL, NULL);

	if (conn < 0) {
		printf("tcp accept fail %d\n", -errno);
		nsi_exit(1);
	}

	long long got = 0;

	while (got < arg->total_bytes) {
		int len = zsock_recv(conn, buf, sizeof(buf), 0);

		if (len <= 0) {
			printf("tcp recv fail %d\n", -errno);
			nsi_exit(1);
		}
		got += len;
	}

	zsock_close(conn);
	zsock_close(lsock);
}

static void bench_tcp_bulk(int chunk, long long total_bytes)
{
	struct tcp_arg arg = { .total_bytes = total_bytes };

	k_thread_create(&peer_thread, peer_stack, K_THREAD_STACK_SIZEOF(peer_stack),
			tcp_rx_fn, &arg, NULL, NULL,
			K_PRIO_PREEMPT(8), 0, K_NO_WAIT);

	k_sleep(K_MSEC(50)); /* let listener come up */

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(TCP_PORT),
	};
	int sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	zsock_inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
	if (zsock_connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("tcp connect fail %d\n", -errno);
		nsi_exit(1);
	}

	uint64_t t0 = now_us();
	long long sent = 0;

	while (sent < total_bytes) {
		int want = chunk;

		if (total_bytes - sent < chunk) {
			want = (int)(total_bytes - sent);
		}

		int ret = zsock_send(sock, txbuf, want, 0);

		if (ret < 0) {
			if (errno == EAGAIN || errno == ENOMEM) {
				k_yield();
				continue;
			}
			printf("tcp send fail %d\n", -errno);
			nsi_exit(1);
		}
		sent += ret;
	}

	k_thread_join(&peer_thread, K_FOREVER);

	uint64_t dt = now_us() - t0;

	zsock_close(sock);

	double mbps = (double)total_bytes * 8.0 / (double)dt;

	printf("RESULT tcp_bulk     chunk=%-4d total_mb=%lld time_us=%llu goodput_mbps=%.1f\n",
	       chunk, total_bytes / (1024 * 1024), (unsigned long long)dt, mbps);
}

int main(void)
{
	for (int i = 0; i < sizeof(txbuf); i++) {
		txbuf[i] = (uint8_t)i;
	}

	k_sleep(K_MSEC(500)); /* wait for loopback iface + 127.0.0.1 to be up */

	{
		char *s = nsi_host_getenv("NETBENCH_SCALE");

		if (s != NULL) {
			scale_div = atoi(s);
			if (scale_div < 1) {
				scale_div = 1;
			}
		}
	}

	if (nsi_host_getenv("NETBENCH_TCP_ONLY") != NULL) {
		bench_tcp_bulk(1280, 64LL * 1024 * 1024 / scale_div);
		printf("BENCH DONE\n");
		nsi_exit(0);
	}

	/* warmup */
	bench_udp_pingpong(64, scaled(2000));

	bench_udp_pingpong(32, scaled(50000));
	bench_udp_pingpong(512, scaled(50000));
	bench_udp_pingpong(1280, scaled(50000));

	bench_udp_blast(32, scaled(100000), 16);
	bench_udp_blast(1280, scaled(100000), 16);

	bench_tcp_bulk(1280, 64LL * 1024 * 1024 / scale_div);

	printf("BENCH DONE\n");
	nsi_exit(0);
	return 0;
}
