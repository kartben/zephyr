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
#include <zephyr/zbus/zbus.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include "gui.h"
#include "charge_sim.h"

#include "net_sample_common.h"

#if __POSIX_VISIBLE < 200809
char *strdup(const char *);
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define NO_OF_CONN 2

K_KERNEL_STACK_ARRAY_DEFINE(cp_stk, NO_OF_CONN, 2 * 1024);

static struct k_thread tinfo[NO_OF_CONN];
static k_tid_t tid[NO_OF_CONN];
static char idtag[NO_OF_CONN][25];

static struct charge_sim_state conn_state[NO_OF_CONN];
static struct k_timer gui_update_timer;

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

	ret = sntp_init(&ctx, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
	if (ret < 0) {
		LOG_ERR("Failed to init SNTP IPv4 ctx: %d", ret);
		return ret;
	}

	ret = sntp_query(&ctx, 5000, &stime);
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

ZBUS_CHAN_DEFINE(ch_event,                  /* Name */
		 union ocpp_io_value, NULL, /* Validator */
		 NULL,                      /* User data */
		 ZBUS_OBSERVERS_EMPTY,      /* observers */
		 ZBUS_MSG_INIT(0)           /* Initial value {0} */
);

ZBUS_SUBSCRIBER_DEFINE(cp_thread0, 5);
ZBUS_SUBSCRIBER_DEFINE(cp_thread1, 5);
struct zbus_observer *obs[NO_OF_CONN] = {(struct zbus_observer *)&cp_thread0,
					 (struct zbus_observer *)&cp_thread1};

static void ocpp_cp_entry(void *p1, void *p2, void *p3);
static void update_connector_gui(int conn_idx);
static void gui_update_timer_handler(struct k_timer *timer);
static void button_start_session(int conn_idx);
static void button_stop_session(int conn_idx);

/* Update GUI for a specific connector by calculating current values */
static void update_connector_gui(int conn_idx)
{
	struct charge_sim_params params;

	if (conn_idx < 0 || conn_idx >= NO_OF_CONN) {
		return;
	}

	charge_sim_get_all_params(&conn_state[conn_idx], conn_idx, k_uptime_get(), &params);

	gui_update_connector(conn_idx + 1, params.meter_wh, params.soc_percent, params.voltage_v,
			     params.current_a, params.power_w, conn_state[conn_idx].is_charging);
}

/* Timer callback to periodically update all connector GUIs */
static void gui_update_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	for (int i = 0; i < NO_OF_CONN; i++) {
		update_connector_gui(i);
	}
}

#include <zephyr/sys/cpu_load.h>

/* Busy-wait to simulate business processing and consume CPU for a duration */
static void simulate_business_load(uint32_t duration_ms)
{
	uint64_t start_ms = k_uptime_get();
	volatile uint64_t accumulator = 0U;

	while ((k_uptime_get() - start_ms) < duration_ms) {
		/* Deterministic integer work to keep optimizer from removing the loop */
		accumulator += 0x9E3779B97F4A7C15ULL;
		accumulator ^= (accumulator >> 13);
		accumulator *= 0xff51afd7ed558ccdULL;
	}
}

static int user_notify_cb(enum ocpp_notify_reason reason, union ocpp_io_value *io, void *user_data)
{
	int idx;
	int i;
	uint32_t meter_value;

	switch (reason) {
	case OCPP_USR_GET_METER_VALUE:
		if (OCPP_OMM_ACTIVE_ENERGY_TO_EV == io->meter_val.mes) {
			int cpu_load = cpu_load_metric_get(0);
			LOG_INF("CPU load: %d%%", cpu_load);
			idx = io->meter_val.id_con - 1;
			if (idx >= 0 && idx < NO_OF_CONN) {
				meter_value = charge_sim_get_meter_value(&conn_state[idx], idx,
									 k_uptime_get());
				snprintf(io->meter_val.val, CISTR50, "%u", meter_value);
				LOG_DBG("mtr reading val %s con %d", io->meter_val.val,
					io->meter_val.id_con);
				return 0;
			}
		} else if (OCPP_OMM_ACTIVE_POWER_TO_EV == io->meter_val.mes) {
			/* Return power value with jitter */
			int32_t power_w = charge_sim_get_power(true);
			snprintf(io->meter_val.val, CISTR50, "%d", power_w);
			return 0;
		} else if (OCPP_OMM_CURRENT_TO_EV == io->meter_val.mes) {
			/* Return current value with jitter (in mA) */
			int32_t current_a = charge_sim_get_current(true);
			snprintf(io->meter_val.val, CISTR50, "%d", current_a * 1000);
			return 0;
		} else if (OCPP_OMM_VOLTAGE_AC_RMS == io->meter_val.mes) {
			/* Return voltage value with jitter (in mV) */
			int32_t voltage_v = charge_sim_get_voltage(true);
			snprintf(io->meter_val.val, CISTR50, "%d", voltage_v * 1000);
			return 0;
		} else if (OCPP_OMM_CHARGING_PERCENT == io->meter_val.mes) {
			/* Return state of charge percentage */
			idx = io->meter_val.id_con - 1;
			if (idx >= 0 && idx < NO_OF_CONN) {
				uint8_t soc_percent = charge_sim_get_soc_percent(&conn_state[idx],
										 k_uptime_get());
				snprintf(io->meter_val.val, CISTR50, "%u", soc_percent);
				LOG_DBG("SOC reading val %s con %d", io->meter_val.val,
					io->meter_val.id_con);
				return 0;
			}
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
			LOG_INF("Remote start charging idtag %s connector %d\n", idtag[idx],
				idx + 1);

			strncpy(idtag[idx], io->start_charge.idtag, sizeof(idtag[0]));

			/* Initialize connector state for remote session immediately */
			charge_sim_start_session(&conn_state[idx], idx, k_uptime_get());

			/* Update GUI immediately to show session is active (green) */
			update_connector_gui(idx);
			gui_set_ocpp_status("OCPP: charging");
			gui_show_notification("Charging started");

			tid[idx] = k_thread_create(&tinfo[idx], cp_stk[idx], sizeof(cp_stk[idx]),
						   ocpp_cp_entry, (void *)(uintptr_t)(idx + 1),
						   idtag[idx], obs[idx], 7, 0, K_NO_WAIT);

			char thread_name[32];
			snprintf(thread_name, sizeof(thread_name), "cp_thread_%02d", idx);
			k_thread_name_set(&tinfo[idx], thread_name);

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
	int idx = idcon - 1;
	char *idtag = (char *)p2;
	struct zbus_observer *obs = (struct zbus_observer *)p3;
	ocpp_session_handle_t sh = NULL;
	enum ocpp_auth_status status;
	const uint32_t timeout_ms = 500;

	ret = ocpp_session_open(&sh);
	if (ret < 0) {
		LOG_ERR("ocpp open ses idcon %d> res %d\n", idcon, ret);
		/* Reset charging state and GUI if session open failed */
		idx = idcon - 1;
		if (idx >= 0 && idx < NO_OF_CONN) {
			charge_sim_stop_session(&conn_state[idx]);
			update_connector_gui(idx);
			gui_set_ocpp_status("OCPP: ready");
			gui_show_notification("Session open failed");
		}
		zbus_chan_rm_obs(&ch_event, obs, K_NO_WAIT);
		tid[idcon - 1] = NULL;
		return;
	}

	while (1) {
		/* Avoid quick retry since authorization request is possible only
		 * after Bootnotification process (handled in lib) completed.
		 */

		k_sleep(K_SECONDS(5));
		ret = ocpp_authorize(sh, idtag, &status, timeout_ms);
		if (ret < 0) {
			LOG_ERR("ocpp auth %d> idcon %d status %d\n", ret, idcon, status);
		} else {
			LOG_INF("ocpp auth %d> idcon %d status %d\n", ret, idcon, status);
			break;
		}
	}

	if (status != OCPP_AUTH_ACCEPTED) {
		LOG_ERR("ocpp start idcon %d> not authorized status %d\n", idcon, status);
		/* Reset charging state and GUI if authorization failed */
		idx = idcon - 1;
		if (idx >= 0 && idx < NO_OF_CONN) {
			charge_sim_stop_session(&conn_state[idx]);
			update_connector_gui(idx);
			gui_set_ocpp_status("OCPP: ready");
			gui_show_notification("Authorization failed");
		}
		zbus_chan_rm_obs(&ch_event, obs, K_NO_WAIT);
		ocpp_session_close(sh);
		tid[idcon - 1] = NULL;
		return;
	}

	/* Initialize connector state and calculate start meter value */
	idx = idcon - 1;
	if (idx < 0 || idx >= NO_OF_CONN) {
		LOG_ERR("ocpp start idcon %d> invalid connector id\n", idcon);
		zbus_chan_rm_obs(&ch_event, obs, K_NO_WAIT);
		ocpp_session_close(sh);
		tid[idcon - 1] = NULL;
		return;
	}

	charge_sim_start_session(&conn_state[idx], idx, k_uptime_get());

	ret = ocpp_start_transaction(sh, charge_sim_get_start_meter(&conn_state[idx]), idcon,
				     timeout_ms);
	if (ret != 0) {
		LOG_ERR("ocpp start transaction failed for connector %d: %d", idcon, ret);
		charge_sim_stop_session(&conn_state[idx]);
		zbus_chan_rm_obs(&ch_event, obs, K_NO_WAIT);
		ocpp_session_close(sh);
		tid[idcon - 1] = NULL;
		return;
	}

	gui_set_ocpp_status("OCPP: charging");
	update_connector_gui(idx);

	/* Transaction started successfully, wait for stop event */
	{
		const struct zbus_channel *chan;
		union ocpp_io_value io;
		int add_obs_ret;

		LOG_INF("ocpp start charging connector id %d, start meter: %u Wh\n", idcon,
			charge_sim_get_start_meter(&conn_state[idx]));
		memset(&io, 0, sizeof(io));

		/* Remove observer first in case it was left from a previous session */
		zbus_chan_rm_obs(&ch_event, obs, K_NO_WAIT);

		/* wait for stop charging event from main or remote CS */
		add_obs_ret = zbus_chan_add_obs(&ch_event, obs, K_SECONDS(1));
		if (add_obs_ret < 0) {
			LOG_ERR("Failed to add observer for connector %d: %d", idcon, add_obs_ret);
			uint32_t final_meter =
				charge_sim_get_meter_value(&conn_state[idx], idx, k_uptime_get());
			charge_sim_stop_session(&conn_state[idx]);
			ocpp_stop_transaction(sh, final_meter, timeout_ms);
			ocpp_session_close(sh);
			tid[idcon - 1] = NULL;
			return;
		}

		do {
			const k_timeout_t wait_timeout = K_MSEC(50);
			const uint32_t busy_ms = 20;

			ret = zbus_sub_wait(obs, &chan, wait_timeout);
			if (ret == -EAGAIN) {
				/* Timeout: burn some CPU to simulate business work */
				simulate_business_load(busy_ms);
				continue;
			}
			if (ret < 0) {
				LOG_ERR("Failed to wait for stop charge event: %d", ret);
				continue;
			}

			ret = zbus_chan_read(chan, &io, K_SECONDS(1));
			if (ret < 0) {
				LOG_ERR("Failed to read stop charge event: %d", ret);
				continue;
			}

			LOG_DBG("Received stop event for connector %d, checking against %d",
				io.stop_charge.id_con, idcon);

			if (io.stop_charge.id_con == idcon) {
				LOG_INF("Stop charge event confirmed for connector %d", idcon);
				break;
			} else {
				LOG_DBG("Stop event for different connector, continuing wait");
			}

		} while (1);
	}

	/* Calculate end meter value */
	idx = idcon - 1;
	if (idx < 0 || idx >= NO_OF_CONN) {
		LOG_ERR("ocpp stop idcon %d> invalid connector id\n", idcon);
		/* Retry stop transaction if it returns EAGAIN (state not ready) */
		int retry_count = 0;
		const int max_retries = 10;
		const int retry_delay_ms = 100;

		do {
			ret = ocpp_stop_transaction(sh, 0, timeout_ms);
			if (ret == -EAGAIN && retry_count < max_retries) {
				LOG_DBG("ocpp stop txn idcon %d> EAGAIN, retrying (%d/%d)", idcon,
					retry_count + 1, max_retries);
				k_sleep(K_MSEC(retry_delay_ms));
				retry_count++;
				continue;
			}
			break;
		} while (1);

		if (ret < 0) {
			LOG_ERR("ocpp stop txn idcon %d> %d\n", idcon, ret);
		}
		zbus_chan_rm_obs(&ch_event, obs, K_NO_WAIT);
		ocpp_session_close(sh);
		tid[idcon - 1] = NULL;
		return;
	}

	uint32_t end_meter_wh = charge_sim_get_meter_value(&conn_state[idx], idx, k_uptime_get());
	charge_sim_stop_session(&conn_state[idx]);

	/* Retry stop transaction if it returns EAGAIN (state not ready) */
	int retry_count = 0;
	const int max_retries = 10;
	const int retry_delay_ms = 100;

	do {
		ret = ocpp_stop_transaction(sh, end_meter_wh, timeout_ms);
		if (ret == -EAGAIN && retry_count < max_retries) {
			LOG_DBG("ocpp stop txn idcon %d> EAGAIN, retrying (%d/%d)", idcon,
				retry_count + 1, max_retries);
			k_sleep(K_MSEC(retry_delay_ms));
			retry_count++;
			continue;
		}
		break;
	} while (1);

	if (ret < 0) {
		LOG_ERR("ocpp stop txn idcon %d> %d\n", idcon, ret);
		zbus_chan_rm_obs(&ch_event, obs, K_NO_WAIT);
		ocpp_session_close(sh);
		tid[idcon - 1] = NULL;
		return;
	}

	LOG_INF("ocpp stop charging connector id %d, end meter: %u Wh (start: %u Wh)\n", idcon,
		end_meter_wh, conn_state[idx].start_meter_wh);
	update_connector_gui(idx);
	gui_set_ocpp_status("OCPP: ready");
	k_sleep(K_SECONDS(1));
	zbus_chan_rm_obs(&ch_event, obs, K_NO_WAIT);
	ocpp_session_close(sh);
	tid[idcon - 1] = NULL;
	k_sleep(K_SECONDS(1));
	k_thread_abort(k_current_get());
}

/* Start a charging session on the specified connector */
static void button_start_session(int conn_idx)
{
	if (conn_idx < 0 || conn_idx >= NO_OF_CONN) {
		LOG_ERR("Invalid connector index: %d", conn_idx);
		return;
	}

	/* Check if connector is already charging */
	if (tid[conn_idx] != NULL) {
		LOG_INF("Connector %d already has an active session", conn_idx + 1);
		return;
	}

	LOG_INF("Button: Starting charging session on connector %d", conn_idx + 1);

	/* Initialize connector state immediately */
	charge_sim_start_session(&conn_state[conn_idx], conn_idx, k_uptime_get());

	/* Update GUI immediately to show session is active */
	update_connector_gui(conn_idx);
	gui_set_ocpp_status("OCPP: charging");
	gui_show_notification("Charging started");

	/* Create thread for the charging session */
	tid[conn_idx] = k_thread_create(
		&tinfo[conn_idx], cp_stk[conn_idx], sizeof(cp_stk[conn_idx]), ocpp_cp_entry,
		(void *)(uintptr_t)(conn_idx + 1), idtag[conn_idx], obs[conn_idx], 7, 0, K_NO_WAIT);

	char thread_name[32];
	snprintf(thread_name, sizeof(thread_name), "cp_thread_%02d", conn_idx);
	k_thread_name_set(&tinfo[conn_idx], thread_name);
}

/* Stop a charging session on the specified connector */
static void button_stop_session(int conn_idx)
{
	if (conn_idx < 0 || conn_idx >= NO_OF_CONN) {
		LOG_ERR("Invalid connector index: %d", conn_idx);
		return;
	}

	/* Check if connector is actually charging */
	if (tid[conn_idx] == NULL) {
		LOG_INF("Connector %d has no active session", conn_idx + 1);
		return;
	}

	LOG_INF("Button: Stopping charging session on connector %d", conn_idx + 1);

	/* Send stop charging event via zbus */
	union ocpp_io_value io = {0};
	io.stop_charge.id_con = conn_idx + 1;
	zbus_chan_pub(&ch_event, &io, K_MSEC(2000));
}

/* Input callback handler for button presses */
static void button_input_handler(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	/* Only process key press events (value == 1 means pressed) */
	if (evt->type != INPUT_EV_KEY || evt->value != 1) {
		return;
	}

	LOG_INF("Button pressed: code=%d", evt->code);

	/* Check which connector to toggle */
	/* Default to connector 1 (index 0) for simplicity */
	/* You can modify this to cycle through connectors or use button code to select */
	int conn_idx = 0;

	/* Find first available connector or first charging connector */
	bool found_charging = false;
	for (int i = 0; i < NO_OF_CONN; i++) {
		if (conn_state[i].is_charging && tid[i] != NULL) {
			conn_idx = i;
			found_charging = true;
			break;
		}
	}

	if (found_charging) {
		/* Stop the charging session */
		button_stop_session(conn_idx);
	} else {
		/* Start a charging session on connector 1 */
		button_start_session(0);
	}
}

/* Register input callback for button events */
INPUT_CALLBACK_DEFINE(NULL, button_input_handler, NULL);

static int ocpp_getaddrinfo(char *server, int port, char **ip)
{
	int ret;
	uint8_t retry = 5;
	char addr_str[INET_ADDRSTRLEN];
	struct sockaddr_storage b;
	struct addrinfo *result = NULL;
	struct addrinfo *addr;
	struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};

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
			struct sockaddr_in *broker = ((struct sockaddr_in *)&b);

			broker->sin_addr.s_addr =
				((struct sockaddr_in *)addr->ai_addr)->sin_addr.s_addr;
			broker->sin_family = AF_INET;
			broker->sin_port = htons(port);

			inet_ntop(AF_INET, &broker->sin_addr, addr_str, sizeof(addr_str));

			*ip = strdup(addr_str);
			LOG_INF("IPv4 Address %s", addr_str);
			break;
		}

		LOG_ERR("error: ai_addrlen = %u should be %u or %u", (unsigned int)addr->ai_addrlen,
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
	struct ocpp_cs_info csi = {NULL, CONFIG_NET_SAMPLE_OCPP_WS_PATH,
				   CONFIG_NET_SAMPLE_OCPP_PORT, AF_INET};

	printk("OCPP sample %s\n", CONFIG_BOARD);

	/* Initialize GUI immediately; do not block on network/OCPP */
	gui_init();
	gui_set_ocpp_status("OCPP: init");

	/* Start periodic GUI update timer (reduced rate to cut CPU, 500 ms) */
	k_timer_init(&gui_update_timer, gui_update_timer_handler, NULL);
	k_timer_start(&gui_update_timer, K_MSEC(500), K_MSEC(500));

	wait_for_network();
	gui_set_network_status(true);

	ret = ocpp_getaddrinfo(CONFIG_NET_SAMPLE_OCPP_SERVER, CONFIG_NET_SAMPLE_OCPP_PORT, &ip);
	if (ret < 0) {
		return ret;
	}

	csi.cs_ip = ip;

	ocpp_get_time_from_sntp();

	ret = ocpp_init(&cpi, &csi, user_notify_cb, NULL);
	if (ret < 0) {
		LOG_ERR("ocpp init failed %d\n", ret);
		return ret;
	}
	gui_set_ocpp_status("OCPP: ready");

	/* Initialize connector states */
	for (i = 0; i < NO_OF_CONN; i++) {
		charge_sim_init_state(&conn_state[i], i);
	}

	/* Spawn threads for each connector */
	for (i = 0; i < NO_OF_CONN; i++) {
		snprintf(idtag[i], sizeof(idtag[0]), "ZepId%02d", i);

		tid[i] = k_thread_create(&tinfo[i], cp_stk[i], sizeof(cp_stk[i]), ocpp_cp_entry,
					 (void *)(uintptr_t)(i + 1), idtag[i], obs[i], 7, 0,
					 K_NO_WAIT);

		char thread_name[32];
		snprintf(thread_name, sizeof(thread_name), "cp_thread_%02d", i);
		k_thread_name_set(&tinfo[i], thread_name);
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
