/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "weather_internal.h"

#include <errno.h>

#include <zephyr/kernel.h>

#if defined(CONFIG_SAMPLE_WEATHER_ENABLE_BT) && CONFIG_SAMPLE_WEATHER_ENABLE_BT

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/cts.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(weather_ble, CONFIG_LOG_DEFAULT_LEVEL);

enum weather_ble_state {
	WEATHER_BLE_STATE_READY,
	WEATHER_BLE_STATE_ADVERTISING,
	WEATHER_BLE_STATE_CONNECTED,
	WEATHER_BLE_STATE_ERROR,
};

static struct weather_model *model_ctx;
static enum weather_ble_state ble_state;
static bool cts_notifications_enabled;
static bool temp_notifications_enabled;
static bool humidity_notifications_enabled;

static int16_t encode_temperature_hundredths(const struct weather_model *model)
{
	int32_t value = model->temperature_milli_c / 10;

	value = CLAMP(value, (int32_t)INT16_MIN, (int32_t)INT16_MAX);
	return (int16_t)value;
}

static uint16_t encode_humidity_hundredths(const struct weather_model *model)
{
	int32_t value = model->humidity_milli_pct / 10;

	value = CLAMP(value, 0, 10000);
	return (uint16_t)value;
}

static ssize_t read_temperature(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				uint16_t len, uint16_t offset)
{
	int16_t value = sys_cpu_to_le16(encode_temperature_hundredths(model_ctx));

	ARG_UNUSED(attr);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof(value));
}

static ssize_t read_humidity(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			     uint16_t len, uint16_t offset)
{
	uint16_t value = sys_cpu_to_le16(encode_humidity_hundredths(model_ctx));

	ARG_UNUSED(attr);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof(value));
}

static void temp_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	temp_notifications_enabled = (value == BT_GATT_CCC_NOTIFY);
}

static void humidity_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	humidity_notifications_enabled = (value == BT_GATT_CCC_NOTIFY);
}

BT_GATT_SERVICE_DEFINE(weather_ess_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_ESS),
	BT_GATT_CHARACTERISTIC(BT_UUID_TEMPERATURE, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_temperature, NULL, NULL),
	BT_GATT_CCC(temp_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_HUMIDITY, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_humidity, NULL, NULL),
	BT_GATT_CCC(humidity_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static void connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);

	if (err != 0U) {
		LOG_WRN("Bluetooth connect failed (err 0x%02x)", err);
		ble_state = WEATHER_BLE_STATE_ERROR;
		return;
	}

	ble_state = WEATHER_BLE_STATE_CONNECTED;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(reason);

	ble_state = WEATHER_BLE_STATE_ADVERTISING;
}

BT_CONN_CB_DEFINE(weather_conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void cts_notification_changed(bool enabled)
{
	cts_notifications_enabled = enabled;
}

static int cts_time_write(struct bt_cts_time_format *cts_time)
{
	int64_t unix_ms;
	int err;

	err = bt_cts_time_to_unix_ms(cts_time, &unix_ms);
	if (err != 0) {
		return err;
	}

	weather_model_set_unix_reference(model_ctx, unix_ms);
	return 0;
}

static int cts_fill_current_time(struct bt_cts_time_format *cts_time)
{
	return bt_cts_time_from_unix_ms(cts_time, weather_model_current_unix_ms(model_ctx));
}

static const struct bt_cts_cb cts_cb = {
	.notification_changed = cts_notification_changed,
	.cts_time_write = cts_time_write,
	.fill_current_cts_time = cts_fill_current_time,
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_CTS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_ESS_VAL)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

int weather_ble_init(struct weather_model *model)
{
	int err;

	if (model == NULL) {
		return -EINVAL;
	}

	model_ctx = model;
	ble_state = WEATHER_BLE_STATE_READY;

	err = bt_enable(NULL);
	if (err != 0 && err != -EALREADY) {
		ble_state = WEATHER_BLE_STATE_ERROR;
		return err;
	}

	err = bt_cts_init(&cts_cb);
	if (err != 0) {
		ble_state = WEATHER_BLE_STATE_ERROR;
		return err;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err != 0) {
		ble_state = WEATHER_BLE_STATE_ERROR;
		return err;
	}

	ble_state = WEATHER_BLE_STATE_ADVERTISING;
	return 0;
}

void weather_ble_notify(const struct weather_model *model)
{
	int16_t temp = sys_cpu_to_le16(encode_temperature_hundredths(model));
	uint16_t humidity = sys_cpu_to_le16(encode_humidity_hundredths(model));

	if (ble_state == WEATHER_BLE_STATE_ERROR || model_ctx == NULL) {
		return;
	}

	if (temp_notifications_enabled) {
		(void)bt_gatt_notify(NULL, &weather_ess_svc.attrs[2], &temp, sizeof(temp));
	}

	if (humidity_notifications_enabled) {
		(void)bt_gatt_notify(NULL, &weather_ess_svc.attrs[5], &humidity, sizeof(humidity));
	}

	if (cts_notifications_enabled) {
		(void)bt_cts_send_notification(BT_CTS_UPDATE_REASON_MANUAL);
	}
}

const char *weather_ble_status_string(void)
{
	switch (ble_state) {
	case WEATHER_BLE_STATE_READY:
		return "ready";
	case WEATHER_BLE_STATE_ADVERTISING:
		return "advertising";
	case WEATHER_BLE_STATE_CONNECTED:
		return "connected";
	case WEATHER_BLE_STATE_ERROR:
	default:
		return "error";
	}
}

#else /* CONFIG_SAMPLE_WEATHER_ENABLE_BT */

int weather_ble_init(struct weather_model *model)
{
	ARG_UNUSED(model);
	return 0;
}

void weather_ble_notify(const struct weather_model *model)
{
	ARG_UNUSED(model);
}

const char *weather_ble_status_string(void)
{
	return "disabled";
}

#endif /* CONFIG_SAMPLE_WEATHER_ENABLE_BT */
