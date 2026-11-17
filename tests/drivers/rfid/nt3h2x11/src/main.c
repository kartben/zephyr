/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/rfid.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "rfid_nt3h2x11_emul.h"

#define SRAM_SIZE 64U
#define TARGET_MODE (RFID_MODE_TARGET | RFID_MODE_TX_106 | RFID_MODE_RX_106)

static const struct device *const dev = DEVICE_DT_GET(DT_NODELABEL(nt3h2111));
static const struct emul *const emul = EMUL_DT_GET(DT_NODELABEL(nt3h2111));

/* Reader on the NFC side, run from the system work queue while the host blocks */
struct reader {
	struct k_work_delayable work;
	struct k_sem done;
	bool write;
	int result;
	uint8_t tx[SRAM_SIZE];
	uint8_t rx[SRAM_SIZE];
};

static struct reader reader;

static void reader_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct reader *r = CONTAINER_OF(dwork, struct reader, work);
	int ret;

	nt3h2x11_emul_set_field(emul, true);

	if (r->write) {
		ret = nt3h2x11_emul_reader_write(emul, r->tx);
	} else {
		ret = nt3h2x11_emul_reader_read(emul, r->rx);
	}

	if (ret == -EAGAIN) {
		(void)k_work_reschedule(dwork, K_MSEC(1));
		return;
	}

	r->result = ret;
	k_sem_give(&r->done);
}

static void reader_start(bool write, uint8_t seed)
{
	reader.write = write;
	reader.result = -EINPROGRESS;
	for (size_t i = 0U; i < SRAM_SIZE; i++) {
		reader.tx[i] = (uint8_t)(seed + i);
	}
	memset(reader.rx, 0, sizeof(reader.rx));
	(void)k_work_schedule(&reader.work, K_MSEC(5));
}

static void set_timeout(uint32_t timeout_us)
{
	struct rfid_property prop = {
		.type = RFID_PROP_TIMEOUT,
		.timeout_us = timeout_us,
	};

	zassert_ok(rfid_set_properties(dev, &prop, 1));
	zassert_ok(prop.status);
}

ZTEST(nt3h2x11, test_capabilities)
{
	zassert_equal(rfid_supported_protocols(dev), RFID_PROTO_ISO14443A);
	zassert_equal(rfid_supported_modes(dev, RFID_PROTO_ISO14443B), 0U);
	zassert_true((rfid_supported_modes(dev, RFID_PROTO_ISO14443A) & RFID_MODE_TARGET) != 0U);

	zassert_equal(rfid_set_protocol(dev, RFID_PROTO_ISO14443A,
					RFID_MODE_INITIATOR | RFID_MODE_TX_106 | RFID_MODE_RX_106),
		      -ENOTSUP);
	zassert_equal(rfid_set_protocol(dev, RFID_PROTO_ISO14443A,
					RFID_MODE_TARGET | RFID_MODE_TX_212 | RFID_MODE_RX_212),
		      -ENOTSUP);
	zassert_ok(rfid_set_protocol(dev, RFID_PROTO_ISO14443A, TARGET_MODE));
}

ZTEST(nt3h2x11, test_properties)
{
	struct rfid_property props[] = {
		{.type = RFID_PROP_RF_FIELD},
		{.type = RFID_PROP_SENS_RES},
		{.type = RFID_PROP_SEL_RES},
		{.type = RFID_PROP_UID_SIZE},
		{.type = RFID_PROP_HW_TX_CRC},
	};

	zassert_ok(rfid_get_properties(dev, props, ARRAY_SIZE(props)));
	zassert_ok(props[0].status);
	zassert_false(props[0].rf_on);
	zassert_ok(props[1].status);
	zassert_equal(props[1].sens_res[0], 0x44);
	zassert_equal(props[1].sens_res[1], 0x00);
	zassert_ok(props[2].status);
	zassert_equal(props[2].sel_res, 0x00);
	zassert_ok(props[3].status);
	zassert_equal(props[3].uid_size, 7U);
	zassert_equal(props[4].status, -ENOTSUP);

	nt3h2x11_emul_set_field(emul, true);
	zassert_ok(rfid_get_properties(dev, props, 1));
	zassert_true(props[0].rf_on);
}

ZTEST(nt3h2x11, test_exchange)
{
	const uint8_t answer[] = {0xde, 0xad, 0xbe, 0xef};
	uint8_t expected[SRAM_SIZE] = {0};
	uint8_t rx[SRAM_SIZE];
	uint16_t rx_len = sizeof(rx);

	zassert_ok(rfid_claim(dev));
	set_timeout(USEC_PER_SEC);
	zassert_ok(rfid_set_protocol(dev, RFID_PROTO_ISO14443A, TARGET_MODE));

	/* The reader enters the field after listen started, then writes */
	reader_start(true, 0x10);
	zassert_ok(rfid_target_listen(dev, RFID_PROTO_ISO14443A, rx, &rx_len));
	zassert_ok(k_sem_take(&reader.done, K_SECONDS(1)));
	zassert_ok(reader.result);
	zassert_equal(rx_len, SRAM_SIZE);
	zassert_mem_equal(rx, reader.tx, SRAM_SIZE);

	/* Transmit completes once the reader has read the padded SRAM back */
	reader_start(false, 0);
	zassert_ok(rfid_target_transmit(dev, answer, sizeof(answer), 8U));
	zassert_ok(k_sem_take(&reader.done, K_SECONDS(1)));
	zassert_ok(reader.result);
	memcpy(expected, answer, sizeof(answer));
	zassert_mem_equal(reader.rx, expected, SRAM_SIZE);

	reader_start(true, 0x80);
	rx_len = sizeof(rx);
	zassert_ok(rfid_target_receive(dev, rx, &rx_len));
	zassert_ok(k_sem_take(&reader.done, K_SECONDS(1)));
	zassert_ok(reader.result);
	zassert_mem_equal(rx, reader.tx, SRAM_SIZE);
	zassert_ok(rfid_release(dev));
}

ZTEST(nt3h2x11, test_errors)
{
	uint8_t buf[SRAM_SIZE + 1U] = {0};
	uint16_t len = SRAM_SIZE - 1U;

	zassert_equal(rfid_target_listen(dev, RFID_PROTO_ISO14443A, buf, &len), -ENOSPC);
	zassert_equal(rfid_target_listen(dev, RFID_PROTO_ISO14443B, buf, &len), -ENOTSUP);
	zassert_equal(rfid_target_transmit(dev, buf, sizeof(buf), 8U), -EMSGSIZE);
	zassert_equal(rfid_target_transmit(dev, buf, 1U, 7U), -ENOTSUP);

	/* Pass-through needs a reader in the field */
	len = SRAM_SIZE;
	zassert_equal(rfid_target_receive(dev, buf, &len), -ENOTCONN);
	zassert_equal(rfid_target_transmit(dev, buf, 1U, 8U), -ENOTCONN);

	set_timeout(20U * USEC_PER_MSEC);
	zassert_equal(rfid_target_listen(dev, RFID_PROTO_ISO14443A, buf, &len), -ETIMEDOUT);
}

static void *nt3h2x11_setup(void)
{
	zassert_true(device_is_ready(dev));
	k_work_init_delayable(&reader.work, reader_handler);
	k_sem_init(&reader.done, 0, 1);

	return NULL;
}

static void nt3h2x11_before(void *fixture)
{
	ARG_UNUSED(fixture);

	nt3h2x11_emul_set_field(emul, false);
}

static void nt3h2x11_after(void *fixture)
{
	struct k_work_sync sync;

	ARG_UNUSED(fixture);

	(void)k_work_cancel_delayable_sync(&reader.work, &sync);
	k_sem_reset(&reader.done);
}

ZTEST_SUITE(nt3h2x11, NULL, nt3h2x11_setup, nt3h2x11_before, nt3h2x11_after, NULL);
