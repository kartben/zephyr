/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <time.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/ocpp.h>
#include <zephyr/random/random.h>

#include "net_sample_common.h"

#if __POSIX_VISIBLE < 200809
char    *strdup(const char *);
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define NO_OF_CONN 2
K_KERNEL_STACK_ARRAY_DEFINE(cp_stk, NO_OF_CONN, 2 * 1024);

static struct k_thread tinfo[NO_OF_CONN];
static k_tid_t tid[NO_OF_CONN];
static char idtag[NO_OF_CONN][25];

/* Simple semaphores to signal stop charging to each connector thread */
static K_SEM_DEFINE(stop_sem_0, 0, 1);
static K_SEM_DEFINE(stop_sem_1, 0, 1);
static struct k_sem *stop_sems[NO_OF_CONN] = {&stop_sem_0, &stop_sem_1};

/* Simulated meter readings for realistic charging simulation */
static struct {
	int active_energy_wh;  /* Total energy delivered in Wh */
	int current_amps;      /* Current being delivered in Amps (x10 for precision) */
	bool charging;
} meter_data[NO_OF_CONN];

static int ocpp_get_time_from_sntp(void)
{
	struct sntp_ctx ctx;
	struct sntp_time stime;
	struct sockaddr_in addr;
	struct timespec tv;
	int ret;

	/* ipv4 */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(123);
	inet_pton(AF_INET, CONFIG_NET_SAMPLE_SNTP_SERVER, &addr.sin_addr);

	ret = sntp_init(&ctx, (struct sockaddr *) &addr,
			sizeof(struct sockaddr_in));
	if (ret < 0) {
		LOG_ERR("Failed to init SNTP IPv4 ctx: %d", ret);
		return ret;
	}

	ret = sntp_query(&ctx, 60, &stime);
	if (ret < 0) {
		LOG_ERR("SNTP IPv4 request failed: %d", ret);
		return ret;
	}

	LOG_INF("sntp succ since Epoch: %llu\n", stime.seconds);
	tv.tv_sec = stime.seconds;
	clock_settime(CLOCK_REALTIME, &tv);
	sntp_close(&ctx);
	return 0;
}

static void ocpp_cp_entry(void *p1, void *p2, void *p3);
static int user_notify_cb(enum ocpp_notify_reason reason,
			  union ocpp_io_value *io,
			  void *user_data)
{
	int idx;
	int i;

	switch (reason) {
	case OCPP_USR_GET_METER_VALUE:
		idx = io->meter_val.id_con - 1;
		if (idx < 0 || idx >= NO_OF_CONN) {
			return -EINVAL;
		}

		if (OCPP_OMM_ACTIVE_ENERGY_TO_EV == io->meter_val.mes) {
			snprintf(io->meter_val.val, CISTR50, "%d",
				 meter_data[idx].active_energy_wh);

			LOG_DBG("mtr reading energy %s Wh con %d",
				io->meter_val.val, io->meter_val.id_con);

			return 0;
		} else if (OCPP_OMM_CURRENT_TO_EV == io->meter_val.mes) {
			/* Return current in Amps (divide by 10 for actual value) */
			snprintf(io->meter_val.val, CISTR50, "%d.%d",
				 meter_data[idx].current_amps / 10,
				 meter_data[idx].current_amps % 10);

			LOG_DBG("mtr reading current %s A con %d",
				io->meter_val.val, io->meter_val.id_con);

			return 0;
		}
		break;

	case OCPP_USR_START_CHARGING:
		if (io->start_charge.id_con < 0) {
			for (i = 0; i < NO_OF_CONN; i++) {
				if (tid[i] == NULL) {
					break;
				}
			}

			if (i >= NO_OF_CONN) {
				return -EBUSY;
			}
			idx = i;
		} else {
			idx = io->start_charge.id_con - 1;
		}

		if (tid[idx] == NULL) {
			LOG_INF("Remote start charging idtag %s connector %d\n",
				idtag[idx], idx + 1);

			strncpy(idtag[idx], io->start_charge.idtag,
				sizeof(idtag[0]));

			tid[idx] = k_thread_create(&tinfo[idx], cp_stk[idx],
						   sizeof(cp_stk[idx]), ocpp_cp_entry,
						   (void *)(uintptr_t)(idx + 1), idtag[idx],
						   NULL, 7, 0, K_NO_WAIT);

			return 0;
		}
		break;

	case OCPP_USR_STOP_CHARGING:
		idx = io->stop_charge.id_con - 1;
		if (idx >= 0 && idx < NO_OF_CONN) {
			k_sem_give(stop_sems[idx]);
		}
		return 0;

	case OCPP_USR_UNLOCK_CONNECTOR:
		LOG_INF("unlock connector %d\n", io->unlock_con.id_con);
		return 0;
	}

	return -ENOTSUP;
}

static void ocpp_cp_entry(void *p1, void *p2, void *p3)
{
	int ret;
	int idcon = (int)(uintptr_t)p1;
	char *idtag = (char *)p2;
	int idx = idcon - 1;
	ocpp_session_handle_t sh = NULL;
	enum ocpp_auth_status status;
	const uint32_t timeout_ms = 500;

	/* Initialize meter data for this connector */
	meter_data[idx].active_energy_wh = 0;
	meter_data[idx].current_amps = 0;
	meter_data[idx].charging = false;

	ret = ocpp_session_open(&sh);
	if (ret < 0) {
		LOG_ERR("ocpp open ses idcon %d> res %d\n", idcon, ret);
		return;
	}

	while (1) {
		/* Avoid quick retry since authorization request is possible only
		 * after Bootnotification process (handled in lib) completed.
		 */

		k_sleep(K_SECONDS(5));
		ret = ocpp_authorize(sh,
				     idtag,
				     &status,
				     timeout_ms);
		if (ret < 0) {
			LOG_ERR("ocpp auth %d> idcon %d status %d\n",
				ret, idcon, status);
		} else {
			LOG_INF("ocpp auth %d> idcon %d status %d\n",
				ret, idcon, status);
			break;
		}
	}

	if (status != OCPP_AUTH_ACCEPTED) {
		LOG_ERR("ocpp start idcon %d> not authorized status %d\n",
			idcon, status);
		return;
	}

	ret = ocpp_start_transaction(sh, meter_data[idx].active_energy_wh, idcon, timeout_ms);
	if (ret == 0) {
		LOG_INF("ocpp start charging connector id %d\n", idcon);

		/* Simulate realistic charging */
		meter_data[idx].charging = true;
		meter_data[idx].current_amps = 160;  /* Start with 16.0 Amps */

		/* Wait for stop charging signal or simulate charging for a period */
		while (meter_data[idx].charging) {
			/* Simulate energy accumulation: ~3.7 kW at 230V, 16A */
			/* Every second adds ~1 Wh (3700W / 3600s ≈ 1 Wh/s) */
			k_sleep(K_SECONDS(1));
			meter_data[idx].active_energy_wh += 1;

			/* Vary current slightly to be more realistic */
			if (sys_rand32_get() % 10 == 0) {
				meter_data[idx].current_amps = 155 + (sys_rand32_get() % 10);
			}

			/* Check if stop was signaled */
			if (k_sem_take(stop_sems[idx], K_NO_WAIT) == 0) {
				break;
			}
		}

		meter_data[idx].charging = false;
		meter_data[idx].current_amps = 0;
	}

	ret = ocpp_stop_transaction(sh, meter_data[idx].active_energy_wh, timeout_ms);
	if (ret < 0) {
		LOG_ERR("ocpp stop txn idcon %d> %d\n", idcon, ret);
		return;
	}

	LOG_INF("ocpp stop charging connector id %d\n", idcon);
	k_sleep(K_SECONDS(1));
	ocpp_session_close(sh);
	tid[idx] = NULL;
	k_sleep(K_SECONDS(1));
	k_thread_abort(k_current_get());
}

static int ocpp_getaddrinfo(char *server, int port, char **ip)
{
	int ret;
	uint8_t retry = 5;
	char addr_str[INET_ADDRSTRLEN];
	struct sockaddr_storage b;
	struct addrinfo *result = NULL;
	struct addrinfo *addr;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};

	LOG_INF("cs server %s %d", server, port);
	do {
		ret = getaddrinfo(server, NULL, &hints, &result);
		if (ret == -EAGAIN) {
			LOG_ERR("ERROR: getaddrinfo %d, rebind", ret);
			k_sleep(K_SECONDS(1));
		} else if (ret != 0) {
			LOG_ERR("ERROR: getaddrinfo failed %d", ret);
			return ret;
		}
	} while (--retry && ret);

	addr = result;
	while (addr != NULL) {
		/* IPv4 Address. */
		if (addr->ai_addrlen == sizeof(struct sockaddr_in)) {
			struct sockaddr_in *broker =
				((struct sockaddr_in *)&b);

			broker->sin_addr.s_addr =
				((struct sockaddr_in *)addr->ai_addr)
				->sin_addr.s_addr;
			broker->sin_family = AF_INET;
			broker->sin_port = htons(port);

			inet_ntop(AF_INET, &broker->sin_addr, addr_str,
				  sizeof(addr_str));

			*ip = strdup(addr_str);
			LOG_INF("IPv4 Address %s", addr_str);
			break;
		}

		LOG_ERR("error: ai_addrlen = %u should be %u or %u",
			(unsigned int)addr->ai_addrlen,
			(unsigned int)sizeof(struct sockaddr_in),
			(unsigned int)sizeof(struct sockaddr_in6));

		addr = addr->ai_next;
	}

	/* Free the address. */
	freeaddrinfo(result);

	return 0;
}

int main(void)
{
	int ret;
	int i;
	char *ip = NULL;

	struct ocpp_cp_info cpi = { "basic", "zephyr", .num_of_con = NO_OF_CONN };
	struct ocpp_cs_info csi = { NULL,
				    "/steve/websocket/CentralSystemService/zephyr",
				    CONFIG_NET_SAMPLE_OCPP_PORT,
				    AF_INET };

	printk("OCPP sample %s\n", CONFIG_BOARD);

	wait_for_network();

	ret = ocpp_getaddrinfo(CONFIG_NET_SAMPLE_OCPP_SERVER, CONFIG_NET_SAMPLE_OCPP_PORT, &ip);
	if (ret < 0) {
		return ret;
	}

	csi.cs_ip = ip;

	ocpp_get_time_from_sntp();

	ret = ocpp_init(&cpi,
			&csi,
			user_notify_cb,
			NULL);
	if (ret < 0) {
		LOG_ERR("ocpp init failed %d\n", ret);
		return ret;
	}

	/* Spawn threads for each connector */
	for (i = 0; i < NO_OF_CONN; i++) {
		snprintf(idtag[i], sizeof(idtag[0]), "ZepId%02d", i);

		tid[i] = k_thread_create(&tinfo[i], cp_stk[i],
					 sizeof(cp_stk[i]),
					 ocpp_cp_entry, (void *)(uintptr_t)(i + 1),
					 idtag[i], NULL, 7, 0, K_NO_WAIT);
	}

	/* Active charging session */
	k_sleep(K_SECONDS(30));

	/* Send stop charging to threads */
	for (i = 0; i < NO_OF_CONN; i++) {
		k_sem_give(stop_sems[i]);
		k_sleep(K_SECONDS(1));
	}

	/* User could trigger remote start/stop transaction from CS server */
	k_sleep(K_SECONDS(1200));

	return 0;
}
