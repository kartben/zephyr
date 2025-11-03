/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <time.h>
#include <stdint.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/ocpp.h>
#include <zephyr/random/random.h>
#include <zephyr/zbus/zbus.h>

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

/* Charging session tracking per connector */
struct charging_session {
	int64_t start_time_ms;	/* Session start time in milliseconds */
	uint32_t start_meter_wh; /* Starting meter value in Wh */
	uint32_t last_meter_wh;  /* Last known meter value in Wh */
	bool active;		/* Session active flag */
};

static struct charging_session sessions[NO_OF_CONN];

/* Typical Level 2 charger: 7.4 kW (7400 W) */
#define CHARGING_POWER_W 7400
/* Convert to Wh per second: 7400W / 3600s/h = ~2.056 Wh/s */
#define WH_PER_SECOND ((CHARGING_POWER_W * 10) / 3600) /* Fixed point: x10 */

/* Base meter values for simulation */
#define BASE_METER_VALUE_WH 1000  /* Starting value for first connector */
#define CONNECTOR_OFFSET_WH 500   /* Offset between connectors */
#define MIN_WH_FOR_JITTER 10      /* Minimum consumed Wh before applying jitter */

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

ZBUS_CHAN_DEFINE(ch_event, /* Name */
		 union ocpp_io_value,
		 NULL,			/* Validator */
		 NULL,			/* User data */
		 ZBUS_OBSERVERS_EMPTY,	/* observers */
		 ZBUS_MSG_INIT(0) /* Initial value {0} */
);

ZBUS_SUBSCRIBER_DEFINE(cp_thread0, 5);
ZBUS_SUBSCRIBER_DEFINE(cp_thread1, 5);
struct zbus_observer *obs[NO_OF_CONN] = {(struct zbus_observer *)&cp_thread0,
					 (struct zbus_observer *)&cp_thread1};

static void ocpp_cp_entry(void *p1, void *p2, void *p3);
static int user_notify_cb(enum ocpp_notify_reason reason,
			  union ocpp_io_value *io,
			  void *user_data)
{
	int idx;
	int i;
	uint32_t current_wh;

	switch (reason) {
	case OCPP_USR_GET_METER_VALUE:
		if (OCPP_OMM_ACTIVE_ENERGY_TO_EV == io->meter_val.mes) {
			idx = io->meter_val.id_con - 1;

			/* Validate connector index */
			if (idx >= 0 && idx < NO_OF_CONN) {
				if (sessions[idx].active) {
					/* Active session: calculate current meter value */
					int64_t elapsed_ms = k_uptime_get() - sessions[idx].start_time_ms;
					int64_t elapsed_sec = elapsed_ms / 1000;
					int64_t consumed_wh = ((int64_t)WH_PER_SECOND * elapsed_sec) / 10;

					/* Add small jitter (±3%) for realism if significant time elapsed */
					int32_t jitter = 0;

					if (consumed_wh > MIN_WH_FOR_JITTER) {
						int32_t jitter_percent = (sys_rand32_get() % 7) - 3;

						jitter = (int32_t)((consumed_wh * jitter_percent) / 100);
					}

					/* Calculate final value with overflow protection */
					int64_t final_wh = sessions[idx].start_meter_wh + consumed_wh + jitter;

					if (final_wh > UINT32_MAX) {
						current_wh = UINT32_MAX;
					} else if (final_wh < 0) {
						current_wh = sessions[idx].start_meter_wh;
					} else {
						current_wh = (uint32_t)final_wh;
					}
				} else if (sessions[idx].last_meter_wh > 0) {
					/* Session ended, return last meter value */
					current_wh = sessions[idx].last_meter_wh;
				} else {
					/* No session yet, return default base value */
					current_wh = BASE_METER_VALUE_WH + (idx * CONNECTOR_OFFSET_WH);
				}
			} else {
				/* Invalid connector */
				current_wh = 0;
			}

			snprintf(io->meter_val.val, CISTR50, "%u", current_wh);

			LOG_DBG("mtr reading val %s con %d", io->meter_val.val,
				io->meter_val.id_con);

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
						   obs[idx], 7, 0, K_NO_WAIT);

			return 0;
		}
		break;

	case OCPP_USR_STOP_CHARGING:
		zbus_chan_pub(&ch_event, io, K_MSEC(100));
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
	struct zbus_observer *obs = (struct zbus_observer *)p3;
	ocpp_session_handle_t sh = NULL;
	enum ocpp_auth_status status;
	const uint32_t timeout_ms = 500;
	int idx = idcon - 1;
	uint32_t start_meter_wh, stop_meter_wh;

	/* Validate connector ID */
	if (idx < 0 || idx >= NO_OF_CONN) {
		LOG_ERR("Invalid connector id %d\n", idcon);
		return;
	}

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

	/* Initialize charging session with realistic starting value */
	if (sessions[idx].last_meter_wh > 0) {
		/* Continue from where the last session ended */
		start_meter_wh = sessions[idx].last_meter_wh;
	} else {
		/* First session: use a base value per connector to simulate previous usage */
		start_meter_wh = BASE_METER_VALUE_WH + (idx * CONNECTOR_OFFSET_WH);
	}
	sessions[idx].start_meter_wh = start_meter_wh;
	sessions[idx].start_time_ms = k_uptime_get();
	sessions[idx].active = true;

	ret = ocpp_start_transaction(sh, start_meter_wh, idcon, timeout_ms);
	if (ret == 0) {
		const struct zbus_channel *chan;
		union ocpp_io_value io;

		LOG_INF("ocpp start charging connector id %d\n", idcon);
		memset(&io, 0xff, sizeof(io));

		/* wait for stop charging event from main or remote CS */
		zbus_chan_add_obs(&ch_event, obs, K_SECONDS(1));
		do {
			zbus_sub_wait(obs, &chan, K_FOREVER);
			zbus_chan_read(chan, &io, K_SECONDS(1));

			if (io.stop_charge.id_con == idcon) {
				break;
			}

		} while (1);
	}

	/* Calculate final meter value based on actual elapsed time */
	int64_t elapsed_ms = k_uptime_get() - sessions[idx].start_time_ms;
	int64_t elapsed_sec = elapsed_ms / 1000;
	int64_t consumed_wh = ((int64_t)WH_PER_SECOND * elapsed_sec) / 10;
	int64_t final_wh = sessions[idx].start_meter_wh + consumed_wh;

	/* Ensure no overflow or underflow */
	if (final_wh > UINT32_MAX) {
		stop_meter_wh = UINT32_MAX;
	} else if (final_wh < 0) {
		stop_meter_wh = sessions[idx].start_meter_wh;
	} else {
		stop_meter_wh = (uint32_t)final_wh;
	}

	/* Mark session as inactive and store final meter value */
	sessions[idx].active = false;
	sessions[idx].last_meter_wh = stop_meter_wh;

	ret = ocpp_stop_transaction(sh, stop_meter_wh, timeout_ms);
	if (ret < 0) {
		LOG_ERR("ocpp stop txn idcon %d> %d\n", idcon, ret);
		return;
	}

	LOG_INF("ocpp stop charging connector id %d\n", idcon);
	k_sleep(K_SECONDS(1));
	ocpp_session_close(sh);
	tid[idcon - 1] = NULL;
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
					 idtag[i], obs[i], 7, 0, K_NO_WAIT);
	}

	/* Active charging session */
	k_sleep(K_SECONDS(30));

	/* Send stop charging to thread */
	for (i = 0; i < NO_OF_CONN; i++) {
		union ocpp_io_value io = {0};

		io.stop_charge.id_con = i + 1;

		zbus_chan_pub(&ch_event, &io, K_MSEC(100));
		k_sleep(K_SECONDS(1));
	}

	/* User could trigger remote start/stop transaction from CS server */
	k_sleep(K_SECONDS(1200));

	return 0;
}
