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
#include "gui.h"

#include "net_sample_common.h"

#if __POSIX_VISIBLE < 200809
char *strdup(const char *);
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define NO_OF_CONN          2

#define CHARGING_VOLTAGE_V  480
#define CHARGING_CURRENT_A  120
#define CHARGING_POWER_W    (CHARGING_VOLTAGE_V * CHARGING_CURRENT_A)
/* Base meter reading per connector (Wh) */
#define BASE_METER_WH       10000
/* Battery capacity (assumed 50 kWh for typical EV) */
#define BATTERY_CAPACITY_WH 10000
/* Starting state of charge percentage */
#define START_SOC_PERCENT   20

K_KERNEL_STACK_ARRAY_DEFINE(cp_stk, NO_OF_CONN, 2 * 1024);

static struct k_thread tinfo[NO_OF_CONN];
static k_tid_t tid[NO_OF_CONN];
static char idtag[NO_OF_CONN][25];

/* Per-connector charging session state */
struct connector_state {
	int64_t start_time_ms;     /* Start time in milliseconds */
	uint32_t start_meter_wh;   /* Meter reading at start (Wh) */
	uint8_t start_soc_percent; /* State of charge at start (%) */
	bool is_charging;          /* Whether currently charging */
};

static struct connector_state conn_state[NO_OF_CONN];

/* Last-known telemetry (for GUI updates) */
static uint32_t gui_last_meter_wh[NO_OF_CONN];
static uint8_t gui_last_soc_percent[NO_OF_CONN];
static int gui_last_voltage_v[NO_OF_CONN];
static int gui_last_current_a[NO_OF_CONN];
static int gui_last_power_w[NO_OF_CONN];

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
static void gui_periodic_update_callback(void);
static void button_start_session(int conn_idx);
static void button_stop_session(int conn_idx);

/* Calculate current meter reading with jitter */
static uint32_t calculate_meter_value(int conn_idx)
{
	struct connector_state *state = &conn_state[conn_idx];
	int64_t elapsed_ms;
	double elapsed_hours;
	uint32_t energy_wh;
	uint32_t meter_value;

	if (!state->is_charging || state->start_time_ms == 0) {
		/* Not charging, return base meter reading */
		return BASE_METER_WH + (conn_idx * 1000);
	}

	/* Calculate elapsed time since start */
	elapsed_ms = k_uptime_get() - state->start_time_ms;
	elapsed_hours = (double)elapsed_ms / 3600000.0; /* Convert ms to hours */

	/* Calculate energy consumed: Power (W) * Time (h) = Energy (Wh) */
	energy_wh = (uint32_t)(CHARGING_POWER_W * elapsed_hours);

	/* Add jitter: ±1% of energy consumed */
	/* Generate jitter: -1% to +1% (99% to 101% of energy) */
	int jitter_percent = (sys_rand32_get() % 3) - 1; /* -1, 0, or +1 */
	/* Calculate jitter as signed to handle negative values correctly */
	int32_t jitter_wh_signed = (int32_t)(((int64_t)energy_wh * jitter_percent) / 100);

	/* Current meter reading = start meter + energy consumed (with jitter) */
	/* Use signed arithmetic to handle negative jitter, then clamp to ensure
	 * meter value never goes below start meter value
	 */
	int64_t meter_value_signed =
		(int64_t)state->start_meter_wh + (int64_t)energy_wh + jitter_wh_signed;
	if (meter_value_signed < (int64_t)state->start_meter_wh) {
		meter_value_signed = (int64_t)state->start_meter_wh;
	}
	meter_value = (uint32_t)meter_value_signed;

	return meter_value;
}

/* Calculate current state of charge percentage */
static uint8_t calculate_soc_percent(int conn_idx)
{
	struct connector_state *state = &conn_state[conn_idx];
	int64_t elapsed_ms;
	double elapsed_hours;
	uint32_t energy_wh;
	uint32_t soc_percent;

	if (!state->is_charging || state->start_time_ms == 0) {
		/* Not charging, return starting SOC */
		return state->start_soc_percent;
	}

	/* Calculate elapsed time since start */
	elapsed_ms = k_uptime_get() - state->start_time_ms;
	elapsed_hours = (double)elapsed_ms / 3600000.0; /* Convert ms to hours */

	/* Calculate energy consumed: Power (W) * Time (h) = Energy (Wh) */
	energy_wh = (uint32_t)(CHARGING_POWER_W * elapsed_hours);

	/* Calculate SOC increase: (energy_wh / battery_capacity_wh) * 100 */
	/* SOC = start_soc + (energy_consumed / battery_capacity * 100) */
	soc_percent =
		state->start_soc_percent + (uint32_t)((energy_wh * 100UL) / BATTERY_CAPACITY_WH);

	/* Cap SOC at 100% */
	if (soc_percent > 100) {
		soc_percent = 100;
	}

	return soc_percent;
}

static int user_notify_cb(enum ocpp_notify_reason reason, union ocpp_io_value *io, void *user_data)
{
	int idx;
	int i;
	uint32_t meter_value;

	switch (reason) {
	case OCPP_USR_GET_METER_VALUE:
		if (OCPP_OMM_ACTIVE_ENERGY_TO_EV == io->meter_val.mes) {
			idx = io->meter_val.id_con - 1;
			if (idx >= 0 && idx < NO_OF_CONN) {
				meter_value = calculate_meter_value(idx);
				snprintf(io->meter_val.val, CISTR50, "%u", meter_value);
				LOG_DBG("mtr reading val %s con %d", io->meter_val.val,
					io->meter_val.id_con);
				gui_last_meter_wh[idx] = meter_value;
				gui_update_connector(io->meter_val.id_con, gui_last_meter_wh[idx],
						     gui_last_soc_percent[idx],
						     gui_last_voltage_v[idx],
						     gui_last_current_a[idx], gui_last_power_w[idx],
						     conn_state[idx].is_charging);
				return 0;
			}
		} else if (OCPP_OMM_ACTIVE_POWER_TO_EV == io->meter_val.mes) {
			/* Return power value with jitter */
			int32_t power_w = CHARGING_POWER_W;
			int32_t jitter = (sys_rand32_get() % 401) - 200; /* ±200W jitter */
			power_w = power_w + jitter;
			/* Ensure power doesn't go negative */
			if (power_w < 0) {
				power_w = 0;
			}
			snprintf(io->meter_val.val, CISTR50, "%d", power_w);
			idx = io->meter_val.id_con - 1;
			if (idx >= 0 && idx < NO_OF_CONN) {
				gui_last_power_w[idx] = power_w;
				gui_update_connector(io->meter_val.id_con, gui_last_meter_wh[idx],
						     gui_last_soc_percent[idx],
						     gui_last_voltage_v[idx],
						     gui_last_current_a[idx], gui_last_power_w[idx],
						     conn_state[idx].is_charging);
			}
			return 0;
		} else if (OCPP_OMM_CURRENT_TO_EV == io->meter_val.mes) {
			/* Return current value with jitter (in A) */
			int32_t current_a = CHARGING_CURRENT_A;
			int32_t jitter = (sys_rand32_get() % 3) - 1; /* ±1A jitter */
			current_a = current_a + jitter;
			/* Ensure current doesn't go negative */
			if (current_a < 0) {
				current_a = 0;
			}
			snprintf(io->meter_val.val, CISTR50, "%d", current_a * 1000);
			idx = io->meter_val.id_con - 1;
			if (idx >= 0 && idx < NO_OF_CONN) {
				gui_last_current_a[idx] = current_a;
				gui_update_connector(io->meter_val.id_con, gui_last_meter_wh[idx],
						     gui_last_soc_percent[idx],
						     gui_last_voltage_v[idx],
						     gui_last_current_a[idx], gui_last_power_w[idx],
						     conn_state[idx].is_charging);
			}
			return 0;
		} else if (OCPP_OMM_VOLTAGE_AC_RMS == io->meter_val.mes) {
			/* Return voltage value with jitter (in V) */
			int32_t voltage_v = CHARGING_VOLTAGE_V;
			int32_t jitter = (sys_rand32_get() % 21) - 10; /* ±10V jitter */
			voltage_v = voltage_v + jitter;
			/* Ensure voltage doesn't go negative */
			if (voltage_v < 0) {
				voltage_v = 0;
			}
			snprintf(io->meter_val.val, CISTR50, "%d", voltage_v * 1000);
			idx = io->meter_val.id_con - 1;
			if (idx >= 0 && idx < NO_OF_CONN) {
				gui_last_voltage_v[idx] = voltage_v;
				gui_update_connector(io->meter_val.id_con, gui_last_meter_wh[idx],
						     gui_last_soc_percent[idx],
						     gui_last_voltage_v[idx],
						     gui_last_current_a[idx], gui_last_power_w[idx],
						     conn_state[idx].is_charging);
			}
			return 0;
		} else if (OCPP_OMM_CHARGING_PERCENT == io->meter_val.mes) {
			/* Return state of charge percentage */
			idx = io->meter_val.id_con - 1;
			if (idx >= 0 && idx < NO_OF_CONN) {
				uint8_t soc_percent = calculate_soc_percent(idx);
				snprintf(io->meter_val.val, CISTR50, "%u", soc_percent);
				LOG_DBG("SOC reading val %s con %d", io->meter_val.val,
					io->meter_val.id_con);
				gui_last_soc_percent[idx] = soc_percent;
				gui_update_connector(io->meter_val.id_con, gui_last_meter_wh[idx],
						     gui_last_soc_percent[idx],
						     gui_last_voltage_v[idx],
						     gui_last_current_a[idx], gui_last_power_w[idx],
						     conn_state[idx].is_charging);
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
		LOG_INF("Remote start charging idtag %s connector %d\n", idtag[idx], idx + 1);

		strncpy(idtag[idx], io->start_charge.idtag, sizeof(idtag[0]));

		/* Initialize connector state for remote session immediately */
		conn_state[idx].start_time_ms = k_uptime_get();
		conn_state[idx].start_meter_wh = BASE_METER_WH + (idx * 1000);
		conn_state[idx].start_soc_percent = START_SOC_PERCENT;
		conn_state[idx].is_charging = true;

		/* Update GUI immediately to show session is active (green) */
		gui_update_connector(idx + 1, conn_state[idx].start_meter_wh,
				     conn_state[idx].start_soc_percent, CHARGING_VOLTAGE_V,
				     CHARGING_CURRENT_A, CHARGING_POWER_W, true);
		gui_set_ocpp_status("OCPP: charging");
		gui_show_notification("Charging started");

		tid[idx] = k_thread_create(&tinfo[idx], cp_stk[idx], sizeof(cp_stk[idx]),
					   ocpp_cp_entry, (void *)(uintptr_t)(idx + 1), idtag[idx],
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

/* Periodic GUI update callback - called by LVGL timer every 1 second */
static void gui_periodic_update_callback(void)
{
	/* Update each connector */
	for (int i = 0; i < NO_OF_CONN; i++) {
		uint32_t meter_wh = calculate_meter_value(i);
		uint8_t soc_percent = calculate_soc_percent(i);
		int voltage_v, current_a, power_w;

		if (conn_state[i].is_charging) {
			/* Calculate with jitter similar to callback */
			voltage_v = CHARGING_VOLTAGE_V + ((sys_rand32_get() % 21) - 10);
			if (voltage_v < 0) {
				voltage_v = 0;
			}

			current_a = CHARGING_CURRENT_A + ((sys_rand32_get() % 3) - 1);
			if (current_a < 0) {
				current_a = 0;
			}

			power_w = CHARGING_POWER_W + ((sys_rand32_get() % 401) - 200);
			if (power_w < 0) {
				power_w = 0;
			}
		} else {
			voltage_v = 0;
			current_a = 0;
			power_w = 0;
		}

		/* Update GUI with current values */
		gui_update_connector(i + 1, meter_wh, soc_percent, voltage_v, current_a, power_w,
				     conn_state[i].is_charging);

		/* Update last known values */
		gui_last_meter_wh[i] = meter_wh;
		gui_last_soc_percent[i] = soc_percent;
		gui_last_voltage_v[i] = voltage_v;
		gui_last_current_a[i] = current_a;
		gui_last_power_w[i] = power_w;
	}
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
			conn_state[idx].is_charging = false;
			conn_state[idx].start_time_ms = 0;
			gui_update_connector(idcon, 0, START_SOC_PERCENT, 0, 0, 0, false);
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
		    conn_state[idx].is_charging = false;
		    conn_state[idx].start_time_ms = 0;
		    gui_update_connector(idcon, 0, START_SOC_PERCENT, 0, 0, 0, false);
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

	conn_state[idx].start_time_ms = k_uptime_get();
	/* Start meter = base + connector offset */
	conn_state[idx].start_meter_wh = BASE_METER_WH + (idx * 1000);
	/* Start SOC at 20% */
	conn_state[idx].start_soc_percent = START_SOC_PERCENT;
	conn_state[idx].is_charging = true;

	ret = ocpp_start_transaction(sh, conn_state[idx].start_meter_wh, idcon, timeout_ms);
	if (ret != 0) {
		LOG_ERR("ocpp start transaction failed for connector %d: %d", idcon, ret);
		conn_state[idx].is_charging = false;
		zbus_chan_rm_obs(&ch_event, obs, K_NO_WAIT);
		ocpp_session_close(sh);
		tid[idcon - 1] = NULL;
		return;
	}

	gui_set_ocpp_status("OCPP: charging");
	gui_update_connector(idcon, conn_state[idx].start_meter_wh,
			     conn_state[idx].start_soc_percent, CHARGING_VOLTAGE_V,
			     CHARGING_CURRENT_A, CHARGING_POWER_W, true);

	/* Transaction started successfully, wait for stop event */
	{
		const struct zbus_channel *chan;
		union ocpp_io_value io;
		int add_obs_ret;

		LOG_INF("ocpp start charging connector id %d, start meter: %u Wh\n", idcon,
			conn_state[idx].start_meter_wh);
		memset(&io, 0, sizeof(io));

		/* Remove observer first in case it was left from a previous session */
		zbus_chan_rm_obs(&ch_event, obs, K_NO_WAIT);

		/* wait for stop charging event from main or remote CS */
		add_obs_ret = zbus_chan_add_obs(&ch_event, obs, K_SECONDS(1));
		if (add_obs_ret < 0) {
			LOG_ERR("Failed to add observer for connector %d: %d", idcon, add_obs_ret);
			conn_state[idx].is_charging = false;
			ocpp_stop_transaction(sh, conn_state[idx].start_meter_wh, timeout_ms);
			ocpp_session_close(sh);
			tid[idcon - 1] = NULL;
			return;
		}

		do {
			ret = zbus_sub_wait(obs, &chan, K_FOREVER);
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

	uint32_t end_meter_wh = calculate_meter_value(idx);
	conn_state[idx].is_charging = false;

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
	gui_update_connector(idcon, end_meter_wh, calculate_soc_percent(idx), CHARGING_VOLTAGE_V, 0,
			     0, false);
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
	conn_state[conn_idx].start_time_ms = k_uptime_get();
	conn_state[conn_idx].start_meter_wh = BASE_METER_WH + (conn_idx * 1000);
	conn_state[conn_idx].start_soc_percent = START_SOC_PERCENT;
	conn_state[conn_idx].is_charging = true;

	/* Update GUI immediately to show session is active */
	gui_update_connector(conn_idx + 1, conn_state[conn_idx].start_meter_wh,
			     conn_state[conn_idx].start_soc_percent, CHARGING_VOLTAGE_V,
			     CHARGING_CURRENT_A, CHARGING_POWER_W, true);
	gui_set_ocpp_status("OCPP: charging");
	gui_show_notification("Charging started");

	/* Create thread for the charging session */
	tid[conn_idx] = k_thread_create(
		&tinfo[conn_idx], cp_stk[conn_idx], sizeof(cp_stk[conn_idx]), ocpp_cp_entry,
		(void *)(uintptr_t)(conn_idx + 1), idtag[conn_idx], obs[conn_idx], 7, 0, K_NO_WAIT);
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
	gui_register_periodic_update_callback(gui_periodic_update_callback);

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
	    conn_state[i].start_time_ms = 0;
	    conn_state[i].start_meter_wh = 0;
	    conn_state[i].start_soc_percent = START_SOC_PERCENT;
	    conn_state[i].is_charging = false;
	    gui_last_meter_wh[i] = 0;
	    gui_last_soc_percent[i] = 0;
	    gui_last_voltage_v[i] = 0;
	    gui_last_current_a[i] = 0;
	    gui_last_power_w[i] = 0;
    }

	/* Spawn threads for each connector */
	for (i = 0; i < NO_OF_CONN; i++) {
		snprintf(idtag[i], sizeof(idtag[0]), "ZepId%02d", i);

		tid[i] = k_thread_create(&tinfo[i], cp_stk[i], sizeof(cp_stk[i]), ocpp_cp_entry,
					 (void *)(uintptr_t)(i + 1), idtag[i], obs[i], 7, 0,
					 K_NO_WAIT);
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
