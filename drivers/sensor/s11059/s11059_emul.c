/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT hamamatsu_s11059

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * Cat. No. KPIC1082E08, Dec. 2017 DN, "Register map" section (p.3):
 * https://datasheet4u.com/pdf/1323325/S11059-02DT.pdf
 * (mirror of the Hamamatsu Photonics S11059-02DT/-03DS datasheet, www.hamamatsu.com)
 */
enum {
	CONTROL = 0x00,
	MANUAL_TIMING_MSB = 0x01,
	MANUAL_TIMING_LSB = 0x02,
	SENSOR_DATA_RED_MSB = 0x03,
	SENSOR_DATA_RED_LSB = 0x04,
	SENSOR_DATA_GREEN_MSB = 0x05,
	SENSOR_DATA_GREEN_LSB = 0x06,
	SENSOR_DATA_BLUE_MSB = 0x07,
	SENSOR_DATA_BLUE_LSB = 0x08,
	SENSOR_DATA_IR_MSB = 0x09,
	SENSOR_DATA_IR_LSB = 0x0A,
};

static const struct emul_sensor_reg registers[] = {
	/*
	 * Bit 7 ADC reset (1=reset, 0=operation), bit 6 standby (1=standby, 0=operating),
	 * bit 5 standby function monitor (RO, "1" means standby mode), bit 4 unused, bit 3
	 * gain selection (1=high, 0=low), bit 2 integration mode (1=manual setting,
	 * 0=fixed period), bits 1:0 integration time setting in fixed period mode. The
	 * datasheet gives no power-on reset value for any register; 0 is the state the
	 * "operation start" sequence (p.6) explicitly drives the device into before use.
	 */
	{CONTROL, "Control", .write_mask = 0xcf},
	{MANUAL_TIMING_MSB, "Manual timing register (MSB)", .write_mask = 0xff},
	{MANUAL_TIMING_LSB, "Manual timing register (LSB)", .write_mask = 0xff},
	{SENSOR_DATA_RED_MSB, "Sensor data register (red, MSB)", EMUL_SENSOR_REG_RO},
	{SENSOR_DATA_RED_LSB, "Sensor data register (red, LSB)", EMUL_SENSOR_REG_RO},
	{SENSOR_DATA_GREEN_MSB, "Sensor data register (green, MSB)", EMUL_SENSOR_REG_RO},
	{SENSOR_DATA_GREEN_LSB, "Sensor data register (green, LSB)", EMUL_SENSOR_REG_RO},
	{SENSOR_DATA_BLUE_MSB, "Sensor data register (blue, MSB)", EMUL_SENSOR_REG_RO},
	{SENSOR_DATA_BLUE_LSB, "Sensor data register (blue, LSB)", EMUL_SENSOR_REG_RO},
	{SENSOR_DATA_IR_MSB, "Sensor data register (infrared, MSB)", EMUL_SENSOR_REG_RO},
	{SENSOR_DATA_IR_LSB, "Sensor data register (infrared, LSB)", EMUL_SENSOR_REG_RO},
};

/*
 * Each color is 16-bit unsigned raw ADC counts, held until the next measurement cycle
 * (note under the register map). Photosensitivity at low gain, integration time 546 ms/ch,
 * typ. (p.2): blue 4.4, green 8.3, red 11.2, infrared 3.0 counts/lx. Sensitivity is linear
 * in integration time (S' = S x Tset/Tmeas, "Compensation method for sensitivity
 * variation", p.6), so lx/count = 546 / (Tint_ms x S_546ms). Control bits 1:0 select one of
 * the four fixed-period integration times (p.3): 00=87.5 us, 01=1.4 ms, 10=22.4 ms,
 * 11=179.2 ms, giving the per-channel lsb variants below (index = bits 1:0).
 *
 * Not modeled: high gain (bit 3, sensitivity x10 typ., p.2 "Gain ratio") is stored but does
 * not scale the emulated reading. Manual setting mode (bit 2 = 1) multiplies the fixed-mode
 * unit time by the 16-bit value of the manual timing register (0x01/0x02, up to 65535,
 * p.3), a continuously variable scale factor that has no fixed number of variants; while
 * manual mode is selected, readings still use the fixed-period lsb for the current bits
 * 1:0, regardless of the manual timing register's contents.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_RED, .reg = SENSOR_DATA_RED_MSB, .bits = 16, .lsb = 557.1429,
	 .select = {CONTROL, GENMASK(1, 0)},
	 .variants = {{.lsb = 557.1429}, {.lsb = 34.82143}, {.lsb = 2.176339}, {.lsb = 0.2720424}},
	 .ready = {CONTROL, BIT(5)}},
	{.chan = SENSOR_CHAN_GREEN, .reg = SENSOR_DATA_GREEN_MSB, .bits = 16, .lsb = 751.8072,
	 .select = {CONTROL, GENMASK(1, 0)},
	 .variants = {{.lsb = 751.8072}, {.lsb = 46.98795}, {.lsb = 2.936747}, {.lsb = 0.3670934}},
	 .ready = {CONTROL, BIT(5)}},
	{.chan = SENSOR_CHAN_BLUE, .reg = SENSOR_DATA_BLUE_MSB, .bits = 16, .lsb = 1418.182,
	 .select = {CONTROL, GENMASK(1, 0)},
	 .variants = {{.lsb = 1418.182}, {.lsb = 88.63636}, {.lsb = 5.539773}, {.lsb = 0.6924716}},
	 .ready = {CONTROL, BIT(5)}},
	{.chan = SENSOR_CHAN_IR, .reg = SENSOR_DATA_IR_MSB, .bits = 16, .lsb = 2080.0,
	 .select = {CONTROL, GENMASK(1, 0)},
	 .variants = {{.lsb = 2080.0}, {.lsb = 130.0}, {.lsb = 8.125}, {.lsb = 1.015625}},
	 .ready = {CONTROL, BIT(5)}},
};

/*
 * "In fixed period mode, measurements are continuously repeated"; standby (bit 6 = 1) is
 * the only documented way to stop conversion ("the device goes into standby mode. The ADC
 * block stops its operation."), modeled by .disabled below. Clearing standby (bit 6: 1->0)
 * resumes operation but is not a bit written as one, so convert_on_write cannot express it;
 * this callback re-converts the retained input on that 1->0 transition instead.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (reg == CONTROL && (old & BIT(6)) != 0U && (data->regs[CONTROL] & BIT(6)) == 0U) {
		emul_sensor_regmap_convert(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true,
	.disabled = {.reg = CONTROL, .mask = BIT(6), .value = BIT(6)}, .write = write);
