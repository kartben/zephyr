/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/pulse_io.h>
#include <zephyr/rtio/rtio.h>

static void pulse_io_iodev_submit_sqe(struct rtio_iodev_sqe *iodev_sqe)
{
	const struct pulse_io_iodev *pio = iodev_sqe->sqe.iodev->data;

	if (pio->chan == NULL) {
		rtio_iodev_sqe_err(iodev_sqe, -EINVAL);
		return;
	}

	pulse_io_iodev_submit(pio->dev, iodev_sqe);
}

const struct rtio_iodev_api pulse_io_iodev_api = {
	.submit = pulse_io_iodev_submit_sqe,
};
