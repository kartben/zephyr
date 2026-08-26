/*
 * POSIX layer microbenchmark harness for native_sim (Zephyr minimal libc,
 * so Zephyr's own POSIX implementation is exercised, not the host glibc).
 *
 * fd/socket/poll paths are exercised through the zvfs and zsock entry
 * points, which are the exact functions the POSIX device_io wrappers
 * alias to (the wrapper itself is a tail call).
 *
 * Instruction counts are collected with callgrind client requests:
 * each BENCH() phase zeroes counters, runs the loop, and dumps stats
 * tagged with the phase name.  Run under:
 *   valgrind --tool=callgrind ./zephyr.exe
 * and parse the per-part "summary:" lines.
 */
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/fdtable.h>
#include <zephyr/zvfs/eventfd.h>

#if defined(__has_include)
#if __has_include(<valgrind/callgrind.h>)
/* Zephyr's native build undefines __linux__ (undef_system_defines.h), which
 * defeats valgrind.h's platform detection; restore it for this include only.
 */
#ifndef __linux__
#define __linux__ 1
#define POSIXBENCH_FAKED_LINUX 1
#endif
#include <valgrind/callgrind.h>
#ifdef POSIXBENCH_FAKED_LINUX
#undef __linux__
#endif
#define HAVE_CALLGRIND 1
#endif
#endif

#ifndef HAVE_CALLGRIND
#define CALLGRIND_ZERO_STATS
#define CALLGRIND_DUMP_STATS_AT(s)
#endif

#define ITERS 2000
#define NET_ITERS 500

#define BENCH(name, iters, ...)                                                                    \
	do {                                                                                       \
		/* warmup one iteration so one-time init is excluded */                            \
		{ __VA_ARGS__; }                                                                   \
		printf("BM_RUN %s iters=%d\n", name, (int)(iters));                                \
		CALLGRIND_ZERO_STATS;                                                              \
		for (int _i = 0; _i < (iters); _i++) {                                             \
			__VA_ARGS__;                                                               \
		}                                                                                  \
		CALLGRIND_DUMP_STATS_AT(name);                                                     \
	} while (0)

static void check(int cond, const char *what)
{
	if (!cond) {
		printf("FATAL: %s failed (errno %d)\n", what, errno);
		exit(1);
	}
}

/* ------------------------------------------------------------------ */

static pthread_mutex_t pmtx = PTHREAD_MUTEX_INITIALIZER;
static struct k_mutex kmtx;
static sem_t psem;
static struct k_sem ksem;
static pthread_cond_t pcond = PTHREAD_COND_INITIALIZER;
static pthread_key_t tls_key;
static pthread_once_t once_ctl = PTHREAD_ONCE_INIT;
static volatile int sink;

static void once_fn(void)
{
	sink++;
}

static void bench_sync(void)
{
	struct timespec ts;

	BENCH("baseline_empty_loop", ITERS, __asm__ volatile("" ::: "memory"));

	BENCH("clock_gettime_monotonic", ITERS, {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		sink += ts.tv_nsec;
	});

	BENCH("clock_gettime_realtime", ITERS, {
		clock_gettime(CLOCK_REALTIME, &ts);
		sink += ts.tv_nsec;
	});

	BENCH("k_uptime_ticks", ITERS, { sink += (int)k_uptime_ticks(); });

	BENCH("pthread_mutex_lock_unlock", ITERS, {
		pthread_mutex_lock(&pmtx);
		pthread_mutex_unlock(&pmtx);
	});

	k_mutex_init(&kmtx);
	BENCH("k_mutex_lock_unlock", ITERS, {
		k_mutex_lock(&kmtx, K_FOREVER);
		k_mutex_unlock(&kmtx);
	});

	check(sem_init(&psem, 0, 1) == 0, "sem_init");
	BENCH("sem_wait_post", ITERS, {
		sem_wait(&psem);
		sem_post(&psem);
	});
	sem_destroy(&psem);

	check(sem_init(&psem, 0, 1) == 0, "sem_init2");
	BENCH("sem_timedwait_available", ITERS, {
		struct timespec abst;

		clock_gettime(CLOCK_REALTIME, &abst);
		abst.tv_sec += 1;
		check(sem_timedwait(&psem, &abst) == 0, "sem_timedwait");
		sem_post(&psem);
	});
	sem_destroy(&psem);

	k_sem_init(&ksem, 1, 1);
	BENCH("k_sem_take_give", ITERS, {
		k_sem_take(&ksem, K_FOREVER);
		k_sem_give(&ksem);
	});

	BENCH("pthread_cond_signal_no_waiter", ITERS, pthread_cond_signal(&pcond));

	BENCH("pthread_self", ITERS, { sink += (int)(uintptr_t)pthread_self(); });

	check(pthread_key_create(&tls_key, NULL) == 0, "key_create");
	check(pthread_setspecific(tls_key, (void *)&sink) == 0, "setspecific");
	BENCH("pthread_key_get", ITERS, { sink += (pthread_getspecific(tls_key) != NULL); });

	BENCH("pthread_once_hot", ITERS, pthread_once(&once_ctl, once_fn));
}

/* ------------------------------------------------------------------ */

static void bench_fd(void)
{
	int efd = zvfs_eventfd(0, ZVFS_EFD_NONBLOCK);
	zvfs_eventfd_t val;
	uint64_t buf;

	check(efd >= 0, "eventfd");

	BENCH("eventfd_write_read", ITERS, {
		zvfs_eventfd_write(efd, 1);
		zvfs_eventfd_read(efd, &val);
	});

	BENCH("fd_write_read_dispatch", ITERS, {
		buf = 1;
		check(zvfs_write(efd, &buf, sizeof(buf), NULL) == sizeof(buf), "write");
		check(zvfs_read(efd, &buf, sizeof(buf), NULL) == sizeof(buf), "read");
	});

	zvfs_close(efd);
}

/* ------------------------------------------------------------------ */

static void bench_poll(void)
{
	int efds[32];
	struct zsock_pollfd pfds[32];
	zvfs_eventfd_t v;

	for (int i = 0; i < 32; i++) {
		efds[i] = zvfs_eventfd(0, ZVFS_EFD_NONBLOCK);
		check(efds[i] >= 0, "eventfd[n]");
	}

	/* one ready fd */
	zvfs_eventfd_write(efds[0], 1);

	for (int n = 1; n <= 32; n *= 4) { /* 1, 4, 16 */
		char name[64];

		for (int i = 0; i < n; i++) {
			pfds[i].fd = efds[i];
			pfds[i].events = ZSOCK_POLLIN;
		}
		snprintf(name, sizeof(name), "poll_%dfd_1ready_t0", n);
		BENCH(name, ITERS, { sink += zsock_poll(pfds, n, 0); });
	}

	for (int i = 0; i < 32; i++) {
		pfds[i].fd = efds[i];
		pfds[i].events = ZSOCK_POLLIN;
	}
	BENCH("poll_32fd_1ready_t0", ITERS, { sink += zsock_poll(pfds, 32, 0); });

	/* no ready fd, timeout 0 */
	zvfs_eventfd_read(efds[0], &v);

	for (int n = 1; n <= 32; n *= 4) {
		char name[64];

		for (int i = 0; i < n; i++) {
			pfds[i].fd = efds[i];
			pfds[i].events = ZSOCK_POLLIN;
		}
		snprintf(name, sizeof(name), "poll_%dfd_0ready_t0", n);
		BENCH(name, ITERS, { sink += zsock_poll(pfds, n, 0); });
	}

	/* select, 4 fds, one ready */
	zvfs_eventfd_write(efds[0], 1);
	BENCH("select_4fd_1ready_t0", ITERS, {
		zsock_fd_set rset;
		struct zsock_timeval tv = {0, 0};
		int maxfd = 0;

		ZSOCK_FD_ZERO(&rset);
		for (int i = 0; i < 4; i++) {
			ZSOCK_FD_SET(efds[i], &rset);
			if (efds[i] > maxfd) {
				maxfd = efds[i];
			}
		}
		sink += zsock_select(maxfd + 1, &rset, NULL, NULL, &tv);
	});

	for (int i = 0; i < 32; i++) {
		zvfs_close(efds[i]);
	}
}

/* ------------------------------------------------------------------ */

static void bench_udp(void)
{
	int tx = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int rx = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(4242),
	};
	char buf[512];

	check(tx >= 0 && rx >= 0, "socket");
	zsock_inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
	check(zsock_bind(rx, (struct sockaddr *)&addr, sizeof(addr)) == 0, "bind");

	memset(buf, 0x5a, sizeof(buf));

	BENCH("udp_sendto_recvfrom_32B", NET_ITERS, {
		check(zsock_sendto(tx, buf, 32, 0, (struct sockaddr *)&addr, sizeof(addr)) == 32,
		      "sendto32");
		check(zsock_recv(rx, buf, sizeof(buf), 0) == 32, "recv32");
	});

	BENCH("udp_sendto_recvfrom_512B", NET_ITERS, {
		check(zsock_sendto(tx, buf, 512, 0, (struct sockaddr *)&addr, sizeof(addr)) == 512,
		      "sendto512");
		check(zsock_recv(rx, buf, sizeof(buf), 0) == 512, "recv512");
	});

	/* connected-UDP send() path */
	check(zsock_connect(tx, (struct sockaddr *)&addr, sizeof(addr)) == 0, "connect");
	BENCH("udp_send_connected_32B", NET_ITERS, {
		check(zsock_send(tx, buf, 32, 0) == 32, "send32");
		check(zsock_recv(rx, buf, sizeof(buf), 0) == 32, "recvc32");
	});

	struct iovec iov = {.iov_base = buf, .iov_len = 32};
	struct msghdr msg = {.msg_iov = &iov, .msg_iovlen = 1};

	BENCH("udp_sendmsg_1iov_32B", NET_ITERS, {
		check(zsock_sendmsg(tx, &msg, 0) == 32, "sendmsg32");
		check(zsock_recv(rx, buf, sizeof(buf), 0) == 32, "recvm32");
	});

	zsock_close(tx);
	zsock_close(rx);
}

/* ------------------------------------------------------------------ */

static void bench_tcp(void)
{
	int srv = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	int cli = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(4243),
	};
	char buf[2048];
	int one = 1;

	check(srv >= 0 && cli >= 0, "tcp socket");
	zsock_inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
	check(zsock_bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0, "tcp bind");
	check(zsock_listen(srv, 1) == 0, "listen");
	check(zsock_connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0, "tcp connect");

	int acc = zsock_accept(srv, NULL, NULL);

	check(acc >= 0, "accept");
	zsock_setsockopt(cli, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

	memset(buf, 0x5a, sizeof(buf));

	BENCH("tcp_send_recv_128B", NET_ITERS, {
		int sent = 0, got = 0;

		while (sent < 128) {
			int r = zsock_send(cli, buf + sent, 128 - sent, 0);

			check(r > 0, "tcpsend128");
			sent += r;
		}
		while (got < 128) {
			int r = zsock_recv(acc, buf, sizeof(buf), 0);

			check(r > 0, "tcprecv128");
			got += r;
		}
	});

	BENCH("tcp_send_recv_1024B", NET_ITERS, {
		int sent = 0, got = 0;

		while (sent < 1024) {
			int r = zsock_send(cli, buf + sent, 1024 - sent, 0);

			check(r > 0, "tcpsend1k");
			sent += r;
		}
		while (got < 1024) {
			int r = zsock_recv(acc, buf, sizeof(buf), 0);

			check(r > 0, "tcprecv1k");
			got += r;
		}
	});

	zsock_close(acc);
	zsock_close(cli);
	zsock_close(srv);
}

/* ------------------------------------------------------------------ */

static void bench_misc(void)
{
	struct mq_attr attr = {
		.mq_maxmsg = 4,
		.mq_msgsize = 64,
	};
	mqd_t mq = mq_open("/bmq", O_CREAT | O_RDWR, 0777, &attr);
	char msg[64] = {0};
	unsigned int prio;

	check(mq != (mqd_t)-1, "mq_open");

	BENCH("mq_send_receive_64B", ITERS, {
		check(mq_send(mq, msg, sizeof(msg), 0) == 0, "mq_send");
		check(mq_receive(mq, msg, sizeof(msg), &prio) == (ssize_t)sizeof(msg),
		      "mq_receive");
	});

	mq_close(mq);
	mq_unlink("/bmq");

	struct in_addr ia;
	char abuf[NET_IPV4_ADDR_LEN];

	BENCH("inet_pton_v4", ITERS,
	      { sink += zsock_inet_pton(AF_INET, "192.168.100.200", &ia); });
	BENCH("inet_ntop_v4", ITERS,
	      { sink += (zsock_inet_ntop(AF_INET, &ia, abuf, sizeof(abuf)) != NULL); });

	struct timespec req = {0, 0};

	BENCH("nanosleep_zero", ITERS, nanosleep(&req, NULL));
}

static void *bench_main(void *arg)
{
	ARG_UNUSED(arg);

	bench_sync();
	bench_fd();
	bench_poll();
	bench_udp();
	bench_tcp();
	bench_misc();

	return NULL;
}

static K_THREAD_STACK_DEFINE(bench_stack, 32768);

int main(void)
{
	pthread_t th;
	pthread_attr_t attr;

	printf("POSIXBENCH START (callgrind=%d)\n",
#ifdef HAVE_CALLGRIND
	       1
#else
	       0
#endif
	);

	check(pthread_attr_init(&attr) == 0, "attr_init");
	check(pthread_attr_setstack(&attr, bench_stack, K_THREAD_STACK_SIZEOF(bench_stack)) == 0,
	      "attr_setstack");

	int cret = pthread_create(&th, &attr, bench_main, NULL);

	if (cret != 0) {
		printf("pthread_create ret=%d\n", cret);
	}
	check(cret == 0, "pthread_create");
	check(pthread_join(th, NULL) == 0, "pthread_join");

	printf("POSIXBENCH DONE sink=%d\n", sink);
	{
		extern void nsi_exit(int);

		nsi_exit(0);
	}
	return 0;
}
