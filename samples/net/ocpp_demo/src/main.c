/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * OCPP Demo - Electric Vehicle Charging Station Simulator
 * 
 * This demo simulates a realistic EV charging station with:
 * - 2 connectors supporting different charge speeds
 * - Dynamic power delivery simulation (3.7kW to 22kW)
 * - Realistic meter value reporting (energy, power, current, voltage, temperature)
 * - Multiple charging scenarios (fast charge, slow charge, interrupted charge)
 * - Real-time status updates and monitoring
 */

#include <stdio.h>
#include <time.h>
#include <math.h>

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
K_KERNEL_STACK_ARRAY_DEFINE(cp_stk, NO_OF_CONN, 4 * 1024);

static struct k_thread tinfo[NO_OF_CONN];
static k_tid_t tid[NO_OF_CONN];
static char idtag[NO_OF_CONN][25];

/* Charging simulation parameters */
struct charging_state {
	int connector_id;
	float power_kw;           /* Current power delivery in kW */
	float target_power_kw;    /* Target power delivery in kW */
	int energy_wh;            /* Total energy delivered in Wh */
	int soc_percent;          /* State of charge (0-100%) */
	float temperature_c;      /* Connector temperature in Celsius */
	float voltage_v;          /* AC voltage in V */
	float current_a;          /* Current in A */
	uint32_t start_time;      /* Charging start time */
	bool fast_charge;         /* Fast charge mode */
};

static struct charging_state charge_state[NO_OF_CONN];

/* Initialize charging state for a connector */
static void init_charging_state(int connector_id, bool fast_charge)
{
	int idx = connector_id - 1;
	
	memset(&charge_state[idx], 0, sizeof(struct charging_state));
	charge_state[idx].connector_id = connector_id;
	charge_state[idx].fast_charge = fast_charge;
	charge_state[idx].target_power_kw = fast_charge ? 22.0f : 7.4f;
	charge_state[idx].voltage_v = 230.0f;
	charge_state[idx].temperature_c = 25.0f;
	charge_state[idx].soc_percent = 20; /* Start at 20% SoC */
	
	LOG_INF("Connector %d initialized: %s mode (%.1f kW)",
		connector_id, fast_charge ? "FAST" : "NORMAL",
		charge_state[idx].target_power_kw);
}

/* Simulate charging dynamics - power ramps up, temperature increases */
static void update_charging_state(int connector_id)
{
	int idx = connector_id - 1;
	struct charging_state *cs = &charge_state[idx];
	
	/* Power ramp-up (gradual increase to target) */
	if (cs->power_kw < cs->target_power_kw) {
		cs->power_kw += 0.5f; /* Ramp up 0.5 kW per cycle */
		if (cs->power_kw > cs->target_power_kw) {
			cs->power_kw = cs->target_power_kw;
		}
	}
	
	/* Calculate current based on power and voltage */
	cs->current_a = (cs->power_kw * 1000.0f) / cs->voltage_v;
	
	/* Energy accumulation (assuming 1 second intervals) */
	cs->energy_wh += (int)(cs->power_kw * 1000.0f / 3600.0f);
	
	/* Temperature increase during charging (with saturation) */
	if (cs->temperature_c < 65.0f) {
		cs->temperature_c += (cs->power_kw / 50.0f);
	}
	
	/* State of charge progression */
	if (cs->soc_percent < 95) {
		cs->soc_percent += (sys_rand32_get() % 2); /* Increment 0-1% per cycle */
	}
	
	/* Add some realistic voltage fluctuation */
	cs->voltage_v = 230.0f + ((sys_rand32_get() % 10) - 5) * 0.1f;
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

	switch (reason) {
	case OCPP_USR_GET_METER_VALUE:
		idx = io->meter_val.id_con - 1;
		if (idx < 0 || idx >= NO_OF_CONN) {
			return -EINVAL;
		}

		/* Provide realistic meter values based on measurand type */
		switch (io->meter_val.mes) {
		case OCPP_OMM_ACTIVE_ENERGY_TO_EV:
			snprintf(io->meter_val.val, CISTR50, "%d",
				 charge_state[idx].energy_wh);
			LOG_DBG("Energy delivered to connector %d: %d Wh",
				io->meter_val.id_con, charge_state[idx].energy_wh);
			break;
			
		case OCPP_OMM_ACTIVE_POWER_TO_EV:
			snprintf(io->meter_val.val, CISTR50, "%.1f",
				 charge_state[idx].power_kw * 1000.0f);
			LOG_DBG("Power to connector %d: %.1f W",
				io->meter_val.id_con, charge_state[idx].power_kw * 1000.0f);
			break;
			
		case OCPP_OMM_CURRENT_TO_EV:
			snprintf(io->meter_val.val, CISTR50, "%.2f",
				 charge_state[idx].current_a);
			LOG_DBG("Current to connector %d: %.2f A",
				io->meter_val.id_con, charge_state[idx].current_a);
			break;
			
		case OCPP_OMM_VOLTAGE_AC_RMS:
			snprintf(io->meter_val.val, CISTR50, "%.1f",
				 charge_state[idx].voltage_v);
			LOG_DBG("Voltage at connector %d: %.1f V",
				io->meter_val.id_con, charge_state[idx].voltage_v);
			break;
			
		case OCPP_OMM_TEMPERATURE:
			snprintf(io->meter_val.val, CISTR50, "%.1f",
				 charge_state[idx].temperature_c);
			LOG_DBG("Temperature at connector %d: %.1f C",
				io->meter_val.id_con, charge_state[idx].temperature_c);
			break;
			
		case OCPP_OMM_CHARGING_PERCENT:
			snprintf(io->meter_val.val, CISTR50, "%d",
				 charge_state[idx].soc_percent);
			LOG_INF("🔋 Connector %d SoC: %d%%",
				io->meter_val.id_con, charge_state[idx].soc_percent);
			break;
			
		default:
			/* Return zero for unsupported measurands */
			snprintf(io->meter_val.val, CISTR50, "0");
			break;
		}
		return 0;

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
			LOG_INF("⚡ Remote start charging request - IdTag: %s, Connector: %d",
				io->start_charge.idtag, idx + 1);

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
		LOG_INF("🛑 Remote stop charging request - Connector: %d",
			io->stop_charge.id_con);
		zbus_chan_pub(&ch_event, io, K_MSEC(100));
		return 0;

	case OCPP_USR_UNLOCK_CONNECTOR:
		LOG_INF("🔓 Unlock connector request - Connector: %d",
			io->unlock_con.id_con);
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
	int cycle_count = 0;
	bool fast_charge = (idcon == 1); /* Connector 1 is fast charge */

	/* Initialize charging state */
	init_charging_state(idcon, fast_charge);

	ret = ocpp_session_open(&sh);
	if (ret < 0) {
		LOG_ERR("❌ Failed to open session for connector %d: %d", idcon, ret);
		return;
	}

	LOG_INF("🔌 Session opened for connector %d", idcon);

	while (1) {
		/* Avoid quick retry since authorization request is possible only
		 * after Bootnotification process (handled in lib) completed.
		 */
		k_sleep(K_SECONDS(5));
		
		LOG_INF("🔑 Authorizing IdTag '%s' for connector %d...", idtag, idcon);
		
		ret = ocpp_authorize(sh, idtag, &status, timeout_ms);
		if (ret < 0) {
			LOG_ERR("❌ Authorization failed for connector %d: %d (status: %d)",
				idcon, ret, status);
		} else {
			LOG_INF("✅ Authorization successful for connector %d (status: %d)",
				idcon, status);
			break;
		}
	}

	if (status != OCPP_AUTH_ACCEPTED) {
		LOG_ERR("⛔ Connector %d not authorized (status: %d)", idcon, status);
		ocpp_session_close(sh);
		tid[idcon - 1] = NULL;
		return;
	}

	/* Start transaction with initial meter reading */
	ret = ocpp_start_transaction(sh, charge_state[idcon - 1].energy_wh, 
				     idcon, timeout_ms);
	if (ret == 0) {
		const struct zbus_channel *chan;
		union ocpp_io_value io;
		uint32_t start_time = k_uptime_get_32() / 1000;

		LOG_INF("⚡ CHARGING STARTED on connector %d (%s mode - %.1f kW)",
			idcon, fast_charge ? "FAST" : "NORMAL",
			charge_state[idcon - 1].target_power_kw);
		
		charge_state[idcon - 1].start_time = start_time;
		memset(&io, 0xff, sizeof(io));

		/* wait for stop charging event from main or remote CS */
		zbus_chan_add_obs(&ch_event, obs, K_SECONDS(1));
		
		do {
			/* Update charging dynamics every second */
			update_charging_state(idcon);
			cycle_count++;
			
			/* Log status every 5 seconds */
			if (cycle_count % 5 == 0) {
				uint32_t elapsed = (k_uptime_get_32() / 1000) - start_time;
				LOG_INF("📊 Connector %d Status: %.1f kW | %.1f A | %d Wh | %d%% SoC | %.1f°C | %us",
					idcon,
					charge_state[idcon - 1].power_kw,
					charge_state[idcon - 1].current_a,
					charge_state[idcon - 1].energy_wh,
					charge_state[idcon - 1].soc_percent,
					charge_state[idcon - 1].temperature_c,
					elapsed);
			}
			
			/* Check for stop event with 1 second timeout */
			if (zbus_sub_wait_msg(obs, &chan, &io, K_SECONDS(1)) == 0) {
				if (io.stop_charge.id_con == idcon) {
					LOG_INF("🛑 Stop charging event received for connector %d", idcon);
					break;
				}
			}
			
			/* Auto-stop at 95% SoC or after reasonable time */
			if (charge_state[idcon - 1].soc_percent >= 95) {
				LOG_INF("🔋 Connector %d: Battery full (95%% SoC), stopping charge", idcon);
				break;
			}

		} while (1);
	} else {
		LOG_ERR("❌ Failed to start transaction on connector %d: %d", idcon, ret);
	}

	/* Stop transaction with final meter reading */
	ret = ocpp_stop_transaction(sh, charge_state[idcon - 1].energy_wh, timeout_ms);
	if (ret < 0) {
		LOG_ERR("❌ Failed to stop transaction on connector %d: %d", idcon, ret);
	} else {
		uint32_t total_time = (k_uptime_get_32() / 1000) - charge_state[idcon - 1].start_time;
		LOG_INF("✅ CHARGING COMPLETED on connector %d", idcon);
		LOG_INF("   📈 Final Stats:");
		LOG_INF("      Energy delivered: %d Wh", charge_state[idcon - 1].energy_wh);
		LOG_INF("      Charging time: %u seconds", total_time);
		LOG_INF("      Final SoC: %d%%", charge_state[idcon - 1].soc_percent);
		LOG_INF("      Avg power: %.1f kW", 
			(float)charge_state[idcon - 1].energy_wh / (total_time / 3600.0f) / 1000.0f);
	}

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

	struct ocpp_cp_info cpi = { 
		.model = "ZephyrCharger_v2",
		.vendor = "Zephyr_EV_Systems",
		.num_of_con = NO_OF_CONN,
		.sl_no = "ZC-2025-001",
		.fw_ver = "2.1.0",
		.meter_type = "SmartMeter_Pro"
	};
	
	struct ocpp_cs_info csi = { 
		.cs_ip = NULL,
		.ws_url = "/",  /* cs.ocpp-css.com uses root path */
		.port = CONFIG_NET_SAMPLE_OCPP_PORT,
		.sa_family = AF_INET 
	};

	printk("\n");
	printk("╔═══════════════════════════════════════════════════════╗\n");
	printk("║   OCPP EV Charging Station Demo                      ║\n");
	printk("║   Platform: %s                              ║\n", CONFIG_BOARD);
	printk("║   Connectors: 2 (Fast + Normal)                      ║\n");
	printk("╚═══════════════════════════════════════════════════════╝\n");
	printk("\n");

	LOG_INF("🌐 Waiting for network connection...");
	wait_for_network();
	LOG_INF("✅ Network connected!");

	LOG_INF("🔍 Resolving OCPP server: %s:%d", 
		CONFIG_NET_SAMPLE_OCPP_SERVER, CONFIG_NET_SAMPLE_OCPP_PORT);
	
	ret = ocpp_getaddrinfo(CONFIG_NET_SAMPLE_OCPP_SERVER, 
			       CONFIG_NET_SAMPLE_OCPP_PORT, &ip);
	if (ret < 0) {
		LOG_ERR("❌ Failed to resolve OCPP server");
		return ret;
	}

	csi.cs_ip = ip;
	LOG_INF("✅ Server resolved to: %s", ip);

	LOG_INF("🕐 Synchronizing time via SNTP...");
	ret = ocpp_get_time_from_sntp();
	if (ret < 0) {
		LOG_WRN("⚠️  SNTP sync failed, continuing anyway...");
	} else {
		LOG_INF("✅ Time synchronized!");
	}

	LOG_INF("🔧 Initializing OCPP stack...");
	LOG_INF("   Charge Point: %s (Model: %s)", cpi.vendor, cpi.model);
	LOG_INF("   Central System: %s:%d%s", ip, csi.port, csi.ws_url);
	
	ret = ocpp_init(&cpi, &csi, user_notify_cb, NULL);
	if (ret < 0) {
		LOG_ERR("❌ OCPP initialization failed: %d", ret);
		return ret;
	}
	LOG_INF("✅ OCPP stack initialized!");

	LOG_INF("\n🚀 Starting charging demo scenario...\n");

	/* Scenario: Two vehicles arrive for charging */
	for (i = 0; i < NO_OF_CONN; i++) {
		const char *charge_type = (i == 0) ? "FAST" : "NORMAL";
		
		snprintf(idtag[i], sizeof(idtag[0]), "DemoTag%02d", i + 1);

		LOG_INF("🚗 Vehicle %d arriving at connector %d", i + 1, i + 1);
		LOG_INF("   IdTag: %s | Charge type: %s", idtag[i], charge_type);

		tid[i] = k_thread_create(&tinfo[i], cp_stk[i],
					 sizeof(cp_stk[i]),
					 ocpp_cp_entry, 
					 (void *)(uintptr_t)(i + 1),
					 idtag[i], obs[i], 7, 0, K_NO_WAIT);
		
		/* Stagger the arrivals */
		k_sleep(K_SECONDS(3));
	}

	LOG_INF("\n⏳ Both vehicles charging... (demo will run for 60 seconds)\n");

	/* Let them charge for a while */
	k_sleep(K_SECONDS(60));

	LOG_INF("\n🛑 Initiating controlled shutdown...\n");

	/* Send stop charging to all active connectors */
	for (i = 0; i < NO_OF_CONN; i++) {
		if (tid[i] != NULL) {
			union ocpp_io_value io = {0};
			io.stop_charge.id_con = i + 1;

			LOG_INF("🛑 Stopping connector %d", i + 1);
			zbus_chan_pub(&ch_event, &io, K_MSEC(100));
			k_sleep(K_SECONDS(2));
		}
	}

	LOG_INF("\n✅ Demo scenario completed!");
	LOG_INF("💡 The station is now ready for remote start/stop commands from the Central System");
	
	/* Keep running for remote operations */
	k_sleep(K_SECONDS(1200));

	printk("\n");
	printk("╔═══════════════════════════════════════════════════════╗\n");
	printk("║   Demo session ended                                  ║\n");
	printk("╚═══════════════════════════════════════════════════════╝\n");

	return 0;
}
