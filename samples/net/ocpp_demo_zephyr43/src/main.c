/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <time.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/ocpp.h>
#include <zephyr/random/random.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include "net_sample_common.h"
#include "ocpp_demo.h"

#if __POSIX_VISIBLE < 200809
char *strdup(const char *);
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Global state */
struct system_status g_system_status = {0};
struct connector_info g_connectors[NO_OF_CONN] = {0};
K_MUTEX_DEFINE(g_state_mutex);

/* Thread stacks */
K_KERNEL_STACK_ARRAY_DEFINE(cp_stk, NO_OF_CONN, 2 * 1024);

static struct k_thread tinfo[NO_OF_CONN];
static k_tid_t tid[NO_OF_CONN];

/* GUI update timer */
static struct k_timer gui_update_timer;
static struct k_timer uptime_timer;

static void gui_update_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	
	/* Update system status */
	k_mutex_lock(&g_state_mutex, K_FOREVER);
	g_system_status.cpu_load_percent = cpu_monitor_get_load();
	g_system_status.cpu_freq_mhz = cpu_monitor_get_freq();
	g_system_status.usb_connected = usb_status_is_connected();
	k_mutex_unlock(&g_state_mutex);
	
	/* Update GUI */
	gui_update_all();
}

static void uptime_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	
	k_mutex_lock(&g_state_mutex, K_FOREVER);
	g_system_status.uptime_seconds++;
	k_mutex_unlock(&g_state_mutex);
}

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

	ret = sntp_init(&ctx, (struct sockaddr *)&addr,
			sizeof(struct sockaddr_in));
	if (ret < 0) {
		LOG_ERR("Failed to init SNTP IPv4 ctx: %d", ret);
		return ret;
	}

	ret = sntp_query(&ctx, 60, &stime);
	if (ret < 0) {
		LOG_ERR("SNTP IPv4 request failed: %d", ret);
		sntp_close(&ctx);
		return ret;
	}

	LOG_INF("sntp succ since Epoch: %llu", stime.seconds);
	tv.tv_sec = stime.seconds;
	clock_settime(CLOCK_REALTIME, &tv);
	sntp_close(&ctx);
	return 0;
}

ZBUS_CHAN_DEFINE(ch_event,		 /* Name */
		 union ocpp_io_value,
		 NULL,			 /* Validator */
		 NULL,			 /* User data */
		 ZBUS_OBSERVERS_EMPTY,	 /* observers */
		 ZBUS_MSG_INIT(0)	 /* Initial value {0} */
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
	static int wh = 6 + NO_OF_CONN;
	int idx;
	int i;

	switch (reason) {
	case OCPP_USR_GET_METER_VALUE:
		if (OCPP_OMM_ACTIVE_ENERGY_TO_EV == io->meter_val.mes) {
			snprintf(io->meter_val.val, CISTR50, "%u",
				 wh + io->meter_val.id_con);

			wh++;
			
			/* Update connector meter value in global state */
			if (io->meter_val.id_con > 0 && io->meter_val.id_con <= NO_OF_CONN) {
				k_mutex_lock(&g_state_mutex, K_FOREVER);
				g_connectors[io->meter_val.id_con - 1].meter_value_wh = 
					wh + io->meter_val.id_con;
				k_mutex_unlock(&g_state_mutex);
			}

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
			/* Validate connector ID is in valid range */
			if (io->start_charge.id_con < 1 || io->start_charge.id_con > NO_OF_CONN) {
				return -EINVAL;
			}
			idx = io->start_charge.id_con - 1;
		}

		if (tid[idx] == NULL) {
			LOG_INF("Remote start charging idtag %s connector %d",
				g_connectors[idx].idtag, idx + 1);

			strncpy(g_connectors[idx].idtag, io->start_charge.idtag,
				sizeof(g_connectors[idx].idtag) - 1);

			/* Update connector state */
			k_mutex_lock(&g_state_mutex, K_FOREVER);
			g_connectors[idx].state = CONN_STATE_PREPARING;
			k_mutex_unlock(&g_state_mutex);
			gui_update_connector(idx);

			tid[idx] = k_thread_create(&tinfo[idx], cp_stk[idx],
						    sizeof(cp_stk[idx]), ocpp_cp_entry,
						    (void *)(uintptr_t)(idx + 1),
						    g_connectors[idx].idtag,
						    obs[idx], 7, 0, K_NO_WAIT);

			return 0;
		}
		break;

	case OCPP_USR_STOP_CHARGING:
		zbus_chan_pub(&ch_event, io, K_MSEC(100));
		return 0;

	case OCPP_USR_UNLOCK_CONNECTOR:
		LOG_INF("unlock connector %d", io->unlock_con.id_con);
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
	int conn_idx = idcon - 1;

	ret = ocpp_session_open(&sh);
	if (ret < 0) {
		LOG_ERR("ocpp open ses idcon %d> res %d", idcon, ret);
		k_mutex_lock(&g_state_mutex, K_FOREVER);
		g_connectors[conn_idx].state = CONN_STATE_FAULTED;
		k_mutex_unlock(&g_state_mutex);
		gui_update_connector(conn_idx);
		return;
	}

	while (1) {
		/* Avoid quick retry since authorization request is possible only
		 * after Bootnotification process (handled in lib) completed.
		 */

		k_sleep(K_SECONDS(5));
		ret = ocpp_authorize(sh, idtag, &status, timeout_ms);
		if (ret < 0) {
			LOG_ERR("ocpp auth %d> idcon %d status %d",
				ret, idcon, status);
		} else {
			LOG_INF("ocpp auth %d> idcon %d status %d",
				ret, idcon, status);
			break;
		}
	}

	if (status != OCPP_AUTH_ACCEPTED) {
		LOG_ERR("ocpp start idcon %d> not authorized status %d",
			idcon, status);
		k_mutex_lock(&g_state_mutex, K_FOREVER);
		g_connectors[conn_idx].state = CONN_STATE_FAULTED;
		g_connectors[conn_idx].authorized = false;
		k_mutex_unlock(&g_state_mutex);
		gui_update_connector(conn_idx);
		return;
	}

	k_mutex_lock(&g_state_mutex, K_FOREVER);
	g_connectors[conn_idx].authorized = true;
	k_mutex_unlock(&g_state_mutex);

	uint32_t txn_id = sys_rand32_get();
	ret = ocpp_start_transaction(sh, txn_id, idcon, timeout_ms);
	if (ret == 0) {
		const struct zbus_channel *chan;
		union ocpp_io_value io;

		LOG_INF("ocpp start charging connector id %d", idcon);
		
		/* Update connector state to charging */
		k_mutex_lock(&g_state_mutex, K_FOREVER);
		g_connectors[conn_idx].state = CONN_STATE_CHARGING;
		g_connectors[conn_idx].transaction_id = txn_id;
		k_mutex_unlock(&g_state_mutex);
		gui_update_connector(conn_idx);
		
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
		
		/* Update state to finishing */
		k_mutex_lock(&g_state_mutex, K_FOREVER);
		g_connectors[conn_idx].state = CONN_STATE_FINISHING;
		k_mutex_unlock(&g_state_mutex);
		gui_update_connector(conn_idx);
	}

	ret = ocpp_stop_transaction(sh, sys_rand32_get(), timeout_ms);
	if (ret < 0) {
		LOG_ERR("ocpp stop txn idcon %d> %d", idcon, ret);
		k_mutex_lock(&g_state_mutex, K_FOREVER);
		g_connectors[conn_idx].state = CONN_STATE_FAULTED;
		k_mutex_unlock(&g_state_mutex);
		gui_update_connector(conn_idx);
		return;
	}

	LOG_INF("ocpp stop charging connector id %d", idcon);
	
	/* Reset connector to available */
	k_mutex_lock(&g_state_mutex, K_FOREVER);
	g_connectors[conn_idx].state = CONN_STATE_AVAILABLE;
	g_connectors[conn_idx].transaction_id = 0;
	g_connectors[conn_idx].authorized = false;
	k_mutex_unlock(&g_state_mutex);
	gui_update_connector(conn_idx);
	
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

	struct ocpp_cp_info cpi = {"basic", "zephyr", .num_of_con = NO_OF_CONN};
	struct ocpp_cs_info csi = {NULL,
				   "/steve/websocket/CentralSystemService/zephyr",
				   CONFIG_NET_SAMPLE_OCPP_PORT,
				   AF_INET};

	printk("\n\n");
	printk("================================================\n");
	printk("  Zephyr 4.3 OCPP Demo - STM32F746G Discovery  \n");
	printk("================================================\n");
	printk("Showcasing Zephyr 4.3 Highlights:\n");
	printk("  - OCPP 1.6 Library\n");
	printk("  - USB Device 'Next' Stack\n");
	printk("  - CPU Load Monitoring\n");
	printk("  - LVGL GUI with Touchscreen\n");
	printk("  - Instrumentation Ready\n");
	printk("================================================\n\n");

	/* Initialize global state */
	memset(&g_system_status, 0, sizeof(g_system_status));
	for (i = 0; i < NO_OF_CONN; i++) {
		g_connectors[i].id = i + 1;
		g_connectors[i].state = CONN_STATE_AVAILABLE;
		snprintf(g_connectors[i].idtag, sizeof(g_connectors[i].idtag),
			 "ZepId%02d", i);
		g_connectors[i].meter_value_wh = 0;
		g_connectors[i].transaction_id = 0;
		g_connectors[i].authorized = false;
		tid[i] = NULL;
	}

	/* Initialize subsystems */
	LOG_INF("Initializing subsystems...");
	
	cpu_monitor_init();
	usb_status_init();
	gui_init();

	/* Start GUI update timer */
	k_timer_init(&gui_update_timer, gui_update_timer_handler, NULL);
	k_timer_start(&gui_update_timer, K_SECONDS(1), K_MSEC(500));

	/* Start uptime timer */
	k_timer_init(&uptime_timer, uptime_timer_handler, NULL);
	k_timer_start(&uptime_timer, K_SECONDS(1), K_SECONDS(1));

	/* Wait for network */
	LOG_INF("Waiting for network...");
	wait_for_network();

	/* Update network status */
	struct net_if *iface = net_if_get_default();
	if (iface) {
		struct in_addr *addr = &iface->config.ip.ipv4->unicast[0].address.in_addr;
		inet_ntop(AF_INET, addr, g_system_status.ip_address,
			  sizeof(g_system_status.ip_address));
		k_mutex_lock(&g_state_mutex, K_FOREVER);
		g_system_status.network_connected = true;
		k_mutex_unlock(&g_state_mutex);
		LOG_INF("Network connected: %s", g_system_status.ip_address);
	}

	/* Get OCPP server address */
	ret = ocpp_getaddrinfo(CONFIG_NET_SAMPLE_OCPP_SERVER,
			       CONFIG_NET_SAMPLE_OCPP_PORT, &ip);
	if (ret < 0) {
		LOG_ERR("Failed to resolve OCPP server address");
		return ret;
	}

	csi.cs_ip = ip;

	/* Synchronize time */
	ocpp_get_time_from_sntp();

	/* Initialize OCPP */
	LOG_INF("Initializing OCPP...");
	ret = ocpp_init(&cpi, &csi, user_notify_cb, NULL);
	if (ret < 0) {
		LOG_ERR("ocpp init failed %d", ret);
		return ret;
	}

	k_mutex_lock(&g_state_mutex, K_FOREVER);
	g_system_status.ocpp_connected = true;
	k_mutex_unlock(&g_state_mutex);
	LOG_INF("OCPP connected successfully");

	/* Spawn threads for each connector */
	for (i = 0; i < NO_OF_CONN; i++) {
		tid[i] = k_thread_create(&tinfo[i], cp_stk[i],
					 sizeof(cp_stk[i]),
					 ocpp_cp_entry, (void *)(uintptr_t)(i + 1),
					 g_connectors[i].idtag, obs[i], 7, 0, K_NO_WAIT);
	}

	/* Active charging session for demo */
	k_sleep(K_SECONDS(30));

	/* Send stop charging to threads for demo */
	for (i = 0; i < NO_OF_CONN; i++) {
		union ocpp_io_value io = {0};

		io.stop_charge.id_con = i + 1;

		zbus_chan_pub(&ch_event, &io, K_MSEC(100));
		k_sleep(K_SECONDS(1));
	}

	/* User could trigger remote start/stop transaction from CS server */
	LOG_INF("Demo running. Use touchscreen or Central System for control.");
	k_sleep(K_SECONDS(1200));

	return 0;
}
