/*
 * Copyright (c) 2022 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_ESPRESSIF_PCNT_ESP32_PCNT_ESP32_DECODER_H_
#define ZEPHYR_DRIVERS_SENSOR_ESPRESSIF_PCNT_ESP32_PCNT_ESP32_DECODER_H_

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#ifdef CONFIG_SOC_SERIES_ESP32
#define PCNT_ESP32_MAX_UNITS 8
#else
#define PCNT_ESP32_MAX_UNITS 4
#endif

/* Buffer filled by one read: the counts of all units, taken at one timestamp */
struct pcnt_esp32_encoded_data {
	uint64_t timestamp_ns;
	uint8_t num_units;
	int32_t counts[PCNT_ESP32_MAX_UNITS];
	uint32_t counts_per_rev[PCNT_ESP32_MAX_UNITS];
	/* Devicetree unit index of each entry, matched against the channel index */
	uint8_t unit_idx[PCNT_ESP32_MAX_UNITS];
};

int pcnt_esp32_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder);

#endif /* ZEPHYR_DRIVERS_SENSOR_ESPRESSIF_PCNT_ESP32_PCNT_ESP32_DECODER_H_ */
