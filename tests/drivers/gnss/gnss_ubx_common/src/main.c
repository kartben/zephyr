/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gnss.h>
#include <zephyr/ztest.h>

#include <string.h>

#include "gnss_ubx_common.h"

DEVICE_DEFINE(gnss_dev, "gnss_dev", NULL, NULL, NULL, NULL,
	      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);

static const struct device *gnss_dev = &DEVICE_NAME_GET(gnss_dev);
static struct gnss_ubx_common_data ubx_data;
static struct gnss_data published;
static uint8_t frame_buf[UBX_FRAME_SZ(sizeof(struct ubx_nav_pvt))] __aligned(4);

static void gnss_data_cb(const struct device *dev, const struct gnss_data *data)
{
	published = *data;
}

GNSS_DATA_CALLBACK_DEFINE(&DEVICE_NAME_GET(gnss_dev), gnss_data_cb);

static void publish_pvt(const struct ubx_nav_pvt *nav_pvt)
{
	struct ubx_frame *frame = (struct ubx_frame *)frame_buf;

	frame->class = UBX_CLASS_ID_NAV;
	frame->id = UBX_MSG_ID_NAV_PVT;
	frame->payload_size = sizeof(*nav_pvt);
	memcpy(frame->payload_and_checksum, nav_pvt, sizeof(*nav_pvt));

	memset(&published, 0xFF, sizeof(published));
	gnss_ubx_common_pvt_callback(NULL, frame, UBX_FRAME_SZ(sizeof(*nav_pvt)), &ubx_data);
}

static void receive_dop(const struct ubx_nav_dop *nav_dop)
{
	struct ubx_frame *frame = (struct ubx_frame *)frame_buf;

	frame->class = UBX_CLASS_ID_NAV;
	frame->id = UBX_MSG_ID_NAV_DOP;
	frame->payload_size = sizeof(*nav_dop);
	memcpy(frame->payload_and_checksum, nav_dop, sizeof(*nav_dop));

	gnss_ubx_common_dop_callback(NULL, frame, UBX_FRAME_SZ(sizeof(*nav_dop)), &ubx_data);
}

ZTEST(gnss_ubx_common, test_pvt_3d_fix)
{
	struct ubx_nav_pvt nav_pvt = {
		.fix_type = UBX_NAV_FIX_TYPE_3D,
		.flags = UBX_NAV_PVT_FLAGS_GNSS_FIX_OK,
		.nav = {
			.horiz_acc = 1500,
			.vert_acc = 2500,
			.vel_down = -340,
			.speed_acc = 120,
		},
	};

	publish_pvt(&nav_pvt);

	zassert_equal(published.info.fix_status, GNSS_FIX_STATUS_GNSS_FIX);
	zassert_equal(published.info.fix_type, GNSS_FIX_TYPE_3D);
	zassert_equal(published.info.horizontal_accuracy, 1500);
	zassert_equal(published.info.vertical_accuracy, 2500);
	zassert_equal(published.info.speed_accuracy, 120);
	zassert_true(published.info.vertical_speed_valid);
	zassert_equal(published.nav_data.vertical_speed, 340);
}

ZTEST(gnss_ubx_common, test_pvt_2d_fix)
{
	struct ubx_nav_pvt nav_pvt = {
		.fix_type = UBX_NAV_FIX_TYPE_2D,
		.flags = UBX_NAV_PVT_FLAGS_GNSS_FIX_OK,
	};

	publish_pvt(&nav_pvt);

	zassert_equal(published.info.fix_status, GNSS_FIX_STATUS_GNSS_FIX);
	zassert_equal(published.info.fix_type, GNSS_FIX_TYPE_2D);
	zassert_false(published.info.vertical_speed_valid);
}

ZTEST(gnss_ubx_common, test_pvt_no_fix)
{
	struct ubx_nav_pvt nav_pvt = {
		.fix_type = UBX_NAV_FIX_TYPE_3D,
	};

	publish_pvt(&nav_pvt);

	zassert_equal(published.info.fix_status, GNSS_FIX_STATUS_NO_FIX);
	zassert_equal(published.info.fix_type, GNSS_FIX_TYPE_UNKNOWN);
	zassert_false(published.info.vertical_speed_valid);
}

ZTEST(gnss_ubx_common, test_dop)
{
	const struct ubx_nav_dop nav_dop = {
		.pdop = 150,
		.hdop = 85,
		.vdop = 120,
	};
	struct ubx_nav_pvt nav_pvt = {
		.fix_type = UBX_NAV_FIX_TYPE_3D,
		.flags = UBX_NAV_PVT_FLAGS_GNSS_FIX_OK,
		.nav = {
			.pdop = 150,
		},
	};

	receive_dop(&nav_dop);
	publish_pvt(&nav_pvt);

	zassert_equal(published.info.hdop, 850);
	zassert_equal(published.info.vdop, 1200);
}

static void *gnss_ubx_common_setup(void)
{
	const struct gnss_ubx_common_config config = {
		.gnss = gnss_dev,
	};

	gnss_ubx_common_init(&ubx_data, &config);
	return NULL;
}

ZTEST_SUITE(gnss_ubx_common, NULL, gnss_ubx_common_setup, NULL, NULL, NULL);
