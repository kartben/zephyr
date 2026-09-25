/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "bmm350.h"
#include "bmm350_emul.h"

#define BMM350_NODE DT_NODELABEL(bmm350)

/*
 * Allowed error of a decoded value, in gauss and degC: the driver compensates in steps of 1 uT
 * and 0.01 degC
 */
#define TOLERANCE_GAUSS 0.03
#define TOLERANCE_DEGC  0.02
/* Relative error allowed on top, for strong fields */
#define TOLERANCE_REL   1e-4

#define TIMESTAMP_NS 1234567890123ULL

/* Offset of the X, Y, Z and temperature samples in struct bmm350_raw_mag_data */
#define RAW_OFFSET(i) (2 + (i) * 3)

/* Header channel bits */
#define CHAN_BIT_X    BIT(0)
#define CHAN_BIT_Y    BIT(1)
#define CHAN_BIT_Z    BIT(2)
#define CHAN_BIT_TEMP BIT(3)
#define CHAN_BITS_ALL (CHAN_BIT_X | CHAN_BIT_Y | CHAN_BIT_Z | CHAN_BIT_TEMP)

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(BMM350_NODE);

static const struct sensor_chan_spec chan_x = {SENSOR_CHAN_MAGN_X, 0};
static const struct sensor_chan_spec chan_y = {SENSOR_CHAN_MAGN_Y, 0};
static const struct sensor_chan_spec chan_z = {SENSOR_CHAN_MAGN_Z, 0};
static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_MAGN_XYZ, 0};
static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_DIE_TEMP, 0};

/*
 * Compensation coefficients as the driver derives them from OTP: offsets in uT, t0 in degC,
 * the other coefficients scaled by 1000
 */
static const struct mag_compensate test_comp = {
	.dut_offset_coef = {.t_offs = 500, .offset_x = 5, .offset_y = -3, .offset_z = 2},
	.dut_sensit_coef = {.t_sens = 10, .sens_x = 20, .sens_y = -15, .sens_z = 10},
	.dut_tco = {.tco_x = 30, .tco_y = -20, .tco_z = 10},
	.dut_tcs = {.tcs_x = 5, .tcs_y = -5, .tcs_z = 3},
	.dut_t0 = 23,
	.cross_axis = {.cross_x_y = 10, .cross_y_x = -8, .cross_z_x = 5, .cross_z_y = -4},
};

static struct bmm350_encoded_data edata;

struct expected {
	double mag[3];
	double temp;
};

static double q31_to_double(q31_t value, int8_t shift)
{
	return (double)value / (double)(1LL << (31 - shift));
}

/* Floating point version of the compensation, in gauss and degC */
static void compensate(const int32_t raw[4], const struct mag_compensate *c, struct expected *e)
{
	const double sens[3] = {c->dut_sensit_coef.sens_x, c->dut_sensit_coef.sens_y,
				c->dut_sensit_coef.sens_z};
	const double offs[3] = {c->dut_offset_coef.offset_x, c->dut_offset_coef.offset_y,
				c->dut_offset_coef.offset_z};
	const double tco[3] = {c->dut_tco.tco_x, c->dut_tco.tco_y, c->dut_tco.tco_z};
	const double tcs[3] = {c->dut_tcs.tcs_x, c->dut_tcs.tcs_y, c->dut_tcs.tcs_z};
	const double cxy = c->cross_axis.cross_x_y / 1000.0;
	const double cyx = c->cross_axis.cross_y_x / 1000.0;
	const double czx = c->cross_axis.cross_z_x / 1000.0;
	const double czy = c->cross_axis.cross_z_y / 1000.0;
	const double det = 1.0 - cyx * cxy;
	double m[3];
	double t;
	double dt;

	/* uT and degC */
	m[0] = raw[0] * 0.0071;
	m[1] = raw[1] * 0.0071;
	m[2] = raw[2] * 0.0072;
	t = raw[3] * 0.001;
	if (t > 0.0) {
		t -= 25.49;
	} else if (t < 0.0) {
		t += 25.49;
	}

	t = t * (1.0 + c->dut_sensit_coef.t_sens / 1000.0) + c->dut_offset_coef.t_offs / 1000.0;
	dt = t - c->dut_t0;

	for (int i = 0; i < 3; i++) {
		m[i] = m[i] * (1.0 + sens[i] / 1000.0) + offs[i];
		m[i] += tco[i] * dt / 1000.0;
		m[i] /= 1.0 + tcs[i] * dt / 1000.0;
	}

	/* uT to gauss */
	e->mag[0] = (m[0] - cxy * m[1]) / det / 100.0;
	e->mag[1] = (m[1] - cyx * m[0]) / det / 100.0;
	e->mag[2] = (m[2] + (m[0] * (cyx * czy - czx) - m[1] * (czy - cxy * czx)) / det) / 100.0;
	e->temp = t;
}

static void fill_buffer(const int32_t raw[4], const struct mag_compensate *comp, uint8_t channels,
			bool is_trigger)
{
	memset(&edata, 0, sizeof(edata));
	edata.header.timestamp = TIMESTAMP_NS;
	edata.header.channels = channels;
	edata.header.events = is_trigger ? 1U : 0U;
	edata.comp = *comp;

	for (int i = 0; i < 4; i++) {
		sys_put_le24((uint32_t)raw[i], &edata.payload.buf[RAW_OFFSET(i)]);
	}
}

static double tolerance_get(double expected, double tolerance)
{
	return tolerance + TOLERANCE_REL * ((expected < 0.0) ? -expected : expected);
}

static void check_q31(struct sensor_chan_spec chan, double expected, double tolerance)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;
	double actual;

	memset(&out, 0xa5, sizeof(out));

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, 23);

	actual = q31_to_double(out.readings[0].value, out.shift);
	zassert_within(actual, expected, tolerance_get(expected, tolerance),
		       "channel %d: got %d, expected %d (1e-6)", chan.chan_type,
		       (int)(actual * 1e6), (int)(expected * 1e6));

	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
}

static void check_xyz(const struct expected *e)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	memset(&out, 0xa5, sizeof(out));

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 1);

	/* All readings in one call */
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 4, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, 23);

	for (int i = 0; i < 3; i++) {
		double actual = q31_to_double(out.readings[0].values[i], out.shift);

		zassert_within(actual, e->mag[i], tolerance_get(e->mag[i], TOLERANCE_GAUSS),
			       "axis %d: got %d, expected %d uG", i, (int)(actual * 1e6),
			       (int)(e->mag[i] * 1e6));
	}

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 4, &out), 0);
}

static void check_all_channels(const int32_t raw[4], const struct mag_compensate *comp)
{
	struct expected e;

	compensate(raw, comp, &e);

	check_xyz(&e);
	check_q31(chan_x, e.mag[0], TOLERANCE_GAUSS);
	check_q31(chan_y, e.mag[1], TOLERANCE_GAUSS);
	check_q31(chan_z, e.mag[2], TOLERANCE_GAUSS);
	check_q31(chan_temp, e.temp, TOLERANCE_DEGC);
}

ZTEST(bmm350_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	for (enum sensor_channel chan = SENSOR_CHAN_MAGN_X; chan <= SENSOR_CHAN_MAGN_Z; chan++) {
		zassert_ok(decoder->get_size_info((struct sensor_chan_spec){chan, 0}, &base_size,
						  &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}

	zassert_ok(decoder->get_size_info(chan_temp, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0},
					     &base_size, &frame_size),
		      -ENOTSUP);
}

ZTEST(bmm350_decoder, test_decode_no_comp)
{
	/* 71 uT, 142 uT, -108 uT and 25.00 degC without compensation */
	static const int32_t raw[4] = {10000, 20000, -15000, 50490};
	static const struct mag_compensate no_comp;
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	fill_buffer(raw, &no_comp, CHAN_BITS_ALL, false);
	check_all_channels(raw, &no_comp);

	/* 0.01 G steps: 71 * 2^8 / 100 = 181.76 and -108 * 2^8 / 100 = -276.48 */
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	zassert_equal(out.readings[0].x, 181);
	zassert_equal(out.readings[0].y, 363);
	zassert_equal(out.readings[0].z, -276);
}

ZTEST(bmm350_decoder, test_decode_comp)
{
	static const int32_t raw[4] = {100000, -80000, 60000, 45000};

	fill_buffer(raw, &test_comp, CHAN_BITS_ALL, false);
	check_all_channels(raw, &test_comp);
}

ZTEST(bmm350_decoder, test_decode_full_scale)
{
	/* Largest positive and negative 24-bit samples, negative temperature */
	static const int32_t raw[4] = {8388607, -8388608, -8388608, -40000};

	fill_buffer(raw, &test_comp, CHAN_BITS_ALL, true);
	check_all_channels(raw, &test_comp);
}

ZTEST(bmm350_decoder, test_missing_channels)
{
	static const int32_t raw[4] = {10000, 20000, -15000, 50490};
	static const struct mag_compensate no_comp;
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* One-shot read of SENSOR_CHAN_MAGN_X only */
	fill_buffer(raw, &no_comp, CHAN_BIT_X, false);

	check_q31(chan_x, 0.71, TOLERANCE_GAUSS);

	zassert_equal(decoder->get_frame_count(buffer, chan_y, &frame_count), -ENODATA);
	zassert_equal(decoder->get_frame_count(buffer, chan_xyz, &frame_count), -ENODATA);
	zassert_equal(decoder->get_frame_count(buffer, chan_temp, &frame_count), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_z, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), -ENODATA);
}

ZTEST(bmm350_decoder, test_unsupported)
{
	static const int32_t raw[4] = {10000, 20000, -15000, 50490};
	const struct sensor_chan_spec unsupported[] = {
		{SENSOR_CHAN_ACCEL_XYZ, 0},
		{SENSOR_CHAN_MAGN_XYZ, 1},
		{SENSOR_CHAN_DIE_TEMP, 1},
	};
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit;

	fill_buffer(raw, &test_comp, CHAN_BITS_ALL, false);

	for (size_t i = 0; i < ARRAY_SIZE(unsupported); i++) {
		fit = 0;
		zassert_equal(decoder->get_frame_count(buffer, unsupported[i], &frame_count),
			      -ENOTSUP);
		zassert_equal(decoder->decode(buffer, unsupported[i], &fit, 1, &out), -ENOTSUP);
	}
}

ZTEST(bmm350_decoder, test_max_count)
{
	static const int32_t raw[4] = {10000, 20000, -15000, 50490};
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data out;
	uint32_t fit = 0;

	fill_buffer(raw, &test_comp, CHAN_BITS_ALL, false);

	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 0, &out), 0);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 1);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 0);
}

ZTEST(bmm350_decoder, test_has_trigger)
{
	static const int32_t raw[4];
	const uint8_t *buffer = (const uint8_t *)&edata;

	fill_buffer(raw, &test_comp, CHAN_BITS_ALL, true);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));

	fill_buffer(raw, &test_comp, CHAN_BITS_ALL, false);
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
}

SENSOR_DT_READ_IODEV(bmm350_iodev, BMM350_NODE, {SENSOR_CHAN_MAGN_XYZ, 0},
		     {SENSOR_CHAN_DIE_TEMP, 0});
RTIO_DEFINE(bmm350_rtio, 1, 1);

/* One-shot read through the emulator, compared with the fetch path */
ZTEST(bmm350_decoder, test_read)
{
	const struct device *dev = DEVICE_DT_GET(BMM350_NODE);
	const struct emul *target = EMUL_DT_GET(BMM350_NODE);
	uint8_t buf[sizeof(struct bmm350_encoded_data)];
	struct sensor_three_axis_data out;
	struct sensor_q31_data out_temp;
	struct sensor_value val[3];
	struct sensor_value temp;
	uint16_t frame_count;
	uint32_t fit = 0;

	bmm350_emul_set_mag_raw(target, 30000, -20000, 10000);
	bmm350_emul_set_temp_raw(target, 50490);

	zassert_ok(sensor_sample_fetch(dev));
	zassert_ok(sensor_channel_get(dev, SENSOR_CHAN_MAGN_XYZ, val));
	zassert_ok(sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &temp));

	zassert_ok(sensor_read(&bmm350_iodev, &bmm350_rtio, buf, sizeof(buf)));

	zassert_ok(decoder->get_frame_count(buf, chan_y, &frame_count));
	zassert_equal(frame_count, 1);
	zassert_false(decoder->has_trigger(buf, SENSOR_TRIG_DATA_READY));

	zassert_equal(decoder->decode(buf, chan_xyz, &fit, 1, &out), 1);
	for (int i = 0; i < 3; i++) {
		zassert_within(q31_to_double(out.readings[0].values[i], out.shift),
			       sensor_value_to_double(&val[i]), 0.005, "axis %d mismatch", i);
	}

	fit = 0;
	zassert_equal(decoder->decode(buf, chan_temp, &fit, 1, &out_temp), 1);
	zassert_within(q31_to_double(out_temp.readings[0].value, out_temp.shift),
		       sensor_value_to_double(&temp), 0.005);
}

ZTEST_SUITE(bmm350_decoder, NULL, NULL, NULL, NULL, NULL);
