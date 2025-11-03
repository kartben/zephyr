/*
 * Copyright (c) 2024 Linumiz
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/ocpp.h>
#include <zephyr/random/random.h>

static void test_ocpp_charge_cycle(ocpp_session_handle_t hndl)
{
	int ret;
	int retry = 3;
	enum ocpp_auth_status status;
	const uint32_t timeout_ms = 500;
	static int meter_start_wh = 1000;  /* Start with 1000 Wh */
	static int64_t charge_start_time_ms;

	while (retry--) {
		ret = ocpp_authorize(hndl, "ZepId00", &status, timeout_ms);

		TC_PRINT("auth req ret %d status %d", ret, status);
		k_sleep(K_SECONDS(1));

		if (ret == 0) {
			break;
		}
	}
	zassert_equal(ret, 0, "CP authorize fail %d");
	zassert_equal(status, OCPP_AUTH_ACCEPTED, "idtag not authorized");

	charge_start_time_ms = k_uptime_get();
	ret = ocpp_start_transaction(hndl, meter_start_wh, 1, timeout_ms);
	zassert_equal(ret, 0, "start transaction fail");

	/* Active charging session */
	k_sleep(K_SECONDS(20));

	/* Calculate energy consumed at ~2 Wh/second for 20 seconds */
	int64_t elapsed_ms = k_uptime_get() - charge_start_time_ms;
	int elapsed_seconds = (int)(elapsed_ms / 1000);
	int meter_stop_wh = meter_start_wh + (elapsed_seconds * 2);

	ret = ocpp_stop_transaction(hndl, meter_stop_wh, timeout_ms);
	zassert_equal(ret, 0, "stop transaction fail");

	/* Update starting value for next charge cycle */
	meter_start_wh = meter_stop_wh;
}

static int test_ocpp_user_notify_cb(enum ocpp_notify_reason reason,
				    union ocpp_io_value *io,
				    void *user_data)
{
	static int meter_wh = 1000;  /* Track meter value */
	static int64_t last_read_time_ms;

	switch (reason) {
	case OCPP_USR_GET_METER_VALUE:
		if (OCPP_OMM_ACTIVE_ENERGY_TO_EV == io->meter_val.mes) {
			/* Simulate ~2 Wh/second charging */
			int64_t current_time_ms = k_uptime_get();

			if (last_read_time_ms > 0) {
				int64_t elapsed_ms = current_time_ms - last_read_time_ms;
				int elapsed_seconds = (int)(elapsed_ms / 1000);

				meter_wh += elapsed_seconds * 2;
			}
			last_read_time_ms = current_time_ms;

			snprintf(io->meter_val.val, CISTR50, "%d", meter_wh);

			TC_PRINT("mtr reading val %s con %d",
				 io->meter_val.val,
				 io->meter_val.id_con);
			return 0;
		}
		break;

	case OCPP_USR_START_CHARGING:
		TC_PRINT("start charging idtag %s connector %d\n",
			 io->start_charge.idtag,
			 io->stop_charge.id_con);
		return 0;

	case OCPP_USR_STOP_CHARGING:
		TC_PRINT("stop charging connector %d\n", io->stop_charge.id_con);
		return 0;

	case OCPP_USR_UNLOCK_CONNECTOR:
		TC_PRINT("unlock connector %d\n", io->unlock_con.id_con);
		return 0;
	}

	return -ENOTSUP;
}

int test_ocpp_init(void)
{
	int ret;

	struct ocpp_cp_info cpi = { "basic", "zephyr", .num_of_con = 1 };
	struct ocpp_cs_info csi = { "122.165.245.213", /* ssh.linumiz.com */
				    "/steve/websocket/CentralSystemService/zephyr",
				    8180,
				    AF_INET };

	net_dhcpv4_start(net_if_get_default());

	/* wait for device dhcp ip receive */
	k_sleep(K_SECONDS(3));

	ret = ocpp_init(&cpi,
			&csi,
			test_ocpp_user_notify_cb,
			NULL);
	if (ret < 0) {
		TC_PRINT("ocpp init failed %d\n", ret);
		return ret;
	}

	return 0;
}

ZTEST(net_ocpp, test_ocpp_chargepoint)
{
	int ret;
	ocpp_session_handle_t hndl = NULL;

	ret = test_ocpp_init();
	zassert_equal(ret, 0, "ocpp init failed %d", ret);

	ret = ocpp_session_open(&hndl);
	zassert_equal(ret, 0, "session open failed %d", ret);

	k_sleep(K_SECONDS(2));
	test_ocpp_charge_cycle(hndl);

	ocpp_session_close(hndl);
}

ZTEST_SUITE(net_ocpp, NULL, NULL, NULL, NULL, NULL);
