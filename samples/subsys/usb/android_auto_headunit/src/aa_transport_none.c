/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_transport.h"

#include <errno.h>

#include <zephyr/kernel.h>

/*
 * No way to reach a phone, for a build that never expects one: an emulator, or
 * a board with neither a network nor a USB host port. A peer never attaches,
 * so the protocol layer stays in its link down state and the display belongs
 * to whatever else the sample has to put there.
 */

static int none_open(void)
{
	return 0;
}

static int none_wait_link(k_timeout_t timeout)
{
	k_sleep(timeout);

	return -ETIMEDOUT;
}

static int none_read(uint8_t *buf, size_t len, k_timeout_t timeout)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);

	k_sleep(timeout);

	return -ENOTCONN;
}

static int none_write(const uint8_t *buf, size_t len)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);

	return -ENOTCONN;
}

static void none_close(void)
{
}

static bool none_is_up(void)
{
	return false;
}

static const struct aa_transport none_transport = {
	.open = none_open,
	.wait_link = none_wait_link,
	.read = none_read,
	.write = none_write,
	.close = none_close,
	.is_up = none_is_up,
	.name = "none",
};

const struct aa_transport *aa_transport_get(void)
{
	return &none_transport;
}
