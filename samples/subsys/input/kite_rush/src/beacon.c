/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "beacon.h"

LOG_MODULE_REGISTER(beacon, LOG_LEVEL_INF);

/*
 * The device name carries the best score, or the score of the game in
 * progress, so that any Bluetooth LE scanner app nearby shows it. The scan
 * response holds an Eddystone-URL frame for https://zephyrproject.org.
 */
#define UPDATE_PERIOD_MS 1000

/* What fits next to the flags in a legacy advertisement */
#define NAME_MAX_LEN (BT_GAP_ADV_MAX_ADV_DATA_LEN - 5)

static char name[32];

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, name, 0),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0xaa, 0xfe),
	BT_DATA_BYTES(BT_DATA_SVC_DATA16,
		      0xaa, 0xfe, /* Eddystone */
		      0x10,       /* URL frame */
		      0x00,       /* calibrated TX power at 0 m */
		      0x03,       /* https:// */
		      'z', 'e', 'p', 'h', 'y', 'r', 'p', 'r', 'o', 'j', 'e', 'c', 't',
		      0x08),      /* .org */
};

static int64_t last_update;

static void format_name(const struct game *g)
{
	if (g->phase == PHASE_COUNTDOWN || g->phase == PHASE_FLYING ||
	    g->phase == PHASE_SIGNAL_LOST) {
		snprintf(name, sizeof(name), "Kite Rush %u LV%u", g->score, g->level);
	} else if (g->scores[0].score > 0U) {
		snprintf(name, sizeof(name), "Kite Rush HI %u %.3s", g->scores[0].score,
			 g->scores[0].name);
	} else {
		snprintf(name, sizeof(name), "Kite Rush");
	}

	ad[1].data_len = (uint8_t)MIN(strlen(name), NAME_MAX_LEN);
}

int beacon_init(const struct game *g)
{
	int ret;

	ret = bt_enable(NULL);
	if (ret < 0) {
		LOG_ERR("Bluetooth init failed (%d)", ret);
		return ret;
	}

	format_name(g);
	ret = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (ret < 0) {
		LOG_ERR("Advertising failed to start (%d)", ret);
		return ret;
	}

	LOG_INF("Advertising as \"%s\"", name);

	return 0;
}

void beacon_update(const struct game *g, int64_t now_ms)
{
	char previous[sizeof(name)];
	int ret;

	if (now_ms - last_update < UPDATE_PERIOD_MS) {
		return;
	}
	last_update = now_ms;

	memcpy(previous, name, sizeof(name));
	format_name(g);
	if (strcmp(previous, name) == 0) {
		return;
	}

	ret = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (ret < 0) {
		LOG_WRN("Cannot update the advertising data (%d)", ret);
	}
}
