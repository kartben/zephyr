/*
 * Copyright (c) 2024 Gergo Vari <work@gergovari.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_DS3231_DS3231_H_
#define ZEPHYR_DRIVERS_SENSOR_DS3231_DS3231_H_

#include <stdint.h>

/* Temperature registers */
#define DS3231_REG_TEMP_MSB 0x11
#define DS3231_REG_TEMP_LSB 0x12

/* Temperature bitmasks */
#define DS3231_BITS_TEMP_LSB GENMASK(7, 6) /* fractional portion */

/* Number of significant bits of sensor_ds3231_edata.raw_temp, sign bit included */
#define DS3231_TEMP_BITS 10U

struct sensor_ds3231_header {
	uint64_t timestamp;
} __attribute__((__packed__));

/* Encoded buffer of a read: raw_temp is (MSB << 2) | (LSB >> 6), 0.25 degC/LSB */
struct sensor_ds3231_edata {
	struct sensor_ds3231_header header;
	uint16_t raw_temp;
};

#endif
