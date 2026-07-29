/*
 * Copyright (c) 2026 Benjamin Cabé <benjamin@zephyrproject.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define I2S_DEV_NODE DT_NODELABEL(virtio_sound)

#define SAMPLE_FREQUENCY 44100
#define SAMPLE_BIT_WIDTH 16
#define NUMBER_OF_CHANNELS 2
#define BYTES_PER_SAMPLE (SAMPLE_BIT_WIDTH / 8)
#define BLOCK_SIZE (256)
#define BLOCK_COUNT 8

K_MEM_SLAB_DEFINE_STATIC(tx_slab, BLOCK_SIZE, BLOCK_COUNT, 4);
K_MEM_SLAB_DEFINE_STATIC(rx_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static const struct device *const dev_i2s = DEVICE_DT_GET(I2S_DEV_NODE);

static void fill_default_config(struct i2s_config *cfg, enum i2s_dir dir)
{
	cfg->word_size = SAMPLE_BIT_WIDTH;
	cfg->channels = NUMBER_OF_CHANNELS;
	cfg->format = I2S_FMT_DATA_FORMAT_I2S;
	cfg->options = I2S_OPT_FRAME_CLK_CONTROLLER | I2S_OPT_BIT_CLK_CONTROLLER;
	cfg->frame_clk_freq = SAMPLE_FREQUENCY;
	cfg->block_size = BLOCK_SIZE;
	cfg->timeout = 2000;
	cfg->mem_slab = (dir == I2S_DIR_TX) ? &tx_slab : &rx_slab;
}

/* Leave both directions unconfigured between tests */
static void release_all(void *unused)
{
	struct i2s_config cfg;

	ARG_UNUSED(unused);

	fill_default_config(&cfg, I2S_DIR_TX);
	cfg.frame_clk_freq = 0;
	(void)i2s_configure(dev_i2s, I2S_DIR_TX, &cfg);

	fill_default_config(&cfg, I2S_DIR_RX);
	cfg.frame_clk_freq = 0;
	(void)i2s_configure(dev_i2s, I2S_DIR_RX, &cfg);
}

ZTEST(i2s_virtio_sound, test_device_is_ready)
{
	zassert_true(device_is_ready(dev_i2s), "virtio-sound device is not ready");
}

ZTEST(i2s_virtio_sound, test_configure_accepts_defaults)
{
	struct i2s_config cfg;

	fill_default_config(&cfg, I2S_DIR_TX);
	zassert_ok(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), "default TX config rejected");

	zassert_not_null(i2s_config_get(dev_i2s, I2S_DIR_TX), "config_get returned NULL");
}

ZTEST(i2s_virtio_sound, test_configure_rejects_loopback)
{
	struct i2s_config cfg;

	fill_default_config(&cfg, I2S_DIR_TX);
	cfg.options |= I2S_OPT_LOOPBACK;
	zassert_equal(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), -EINVAL,
		      "loopback should be rejected");

	fill_default_config(&cfg, I2S_DIR_TX);
	cfg.options |= I2S_OPT_PINGPONG;
	zassert_equal(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), -EINVAL,
		      "ping-pong should be rejected");
}

ZTEST(i2s_virtio_sound, test_configure_rejects_bad_parameters)
{
	struct i2s_config cfg;

	fill_default_config(&cfg, I2S_DIR_TX);
	cfg.word_size = 12;
	zassert_equal(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), -EINVAL,
		      "a 12 bit word size should be rejected");

	fill_default_config(&cfg, I2S_DIR_TX);
	cfg.frame_clk_freq = 12345;
	zassert_equal(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), -EINVAL,
		      "a rate outside the virtio table should be rejected");

	fill_default_config(&cfg, I2S_DIR_TX);
	cfg.format |= I2S_FMT_DATA_ORDER_LSB;
	zassert_equal(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), -EINVAL,
		      "LSB first data order should be rejected");

	fill_default_config(&cfg, I2S_DIR_TX);
	cfg.mem_slab = NULL;
	zassert_equal(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), -EINVAL,
		      "a NULL mem_slab should be rejected");

	fill_default_config(&cfg, I2S_DIR_TX);
	cfg.block_size = BLOCK_SIZE + 1;
	zassert_equal(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), -EINVAL,
		      "a block size that is not a multiple of the frame size should be rejected");
}

ZTEST(i2s_virtio_sound, test_configure_dir_both_not_supported)
{
	struct i2s_config cfg;

	fill_default_config(&cfg, I2S_DIR_TX);
	zassert_equal(i2s_configure(dev_i2s, I2S_DIR_BOTH, &cfg), -ENOSYS,
		      "I2S_DIR_BOTH should report -ENOSYS");
}

ZTEST(i2s_virtio_sound, test_unconfigure_returns_to_not_ready)
{
	struct i2s_config cfg;

	fill_default_config(&cfg, I2S_DIR_TX);
	zassert_ok(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), "default TX config rejected");
	zassert_not_null(i2s_config_get(dev_i2s, I2S_DIR_TX), "stream should be configured");

	cfg.frame_clk_freq = 0;
	zassert_ok(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), "un-configure failed");
	zassert_is_null(i2s_config_get(dev_i2s, I2S_DIR_TX),
			"stream should be back to the not ready state");
}

ZTEST(i2s_virtio_sound, test_trigger_from_wrong_state)
{
	struct i2s_config cfg;

	fill_default_config(&cfg, I2S_DIR_TX);
	zassert_ok(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), "default TX config rejected");

	zassert_equal(i2s_trigger(dev_i2s, I2S_DIR_TX, I2S_TRIGGER_START), -EIO,
		      "START without queued data should fail");

	zassert_equal(i2s_trigger(dev_i2s, I2S_DIR_TX, I2S_TRIGGER_STOP), -EIO,
		      "STOP from the ready state should fail");

	zassert_equal(i2s_trigger(dev_i2s, I2S_DIR_TX, I2S_TRIGGER_PREPARE), -EIO,
		      "PREPARE from the ready state should fail");
}

ZTEST(i2s_virtio_sound, test_tx_write_start_drain)
{
	struct i2s_config cfg;
	void *block;
	int ret;

	fill_default_config(&cfg, I2S_DIR_TX);
	zassert_ok(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), "default TX config rejected");

	for (int i = 0; i < 2; i++) {
		zassert_ok(k_mem_slab_alloc(&tx_slab, &block, K_NO_WAIT), "slab allocation failed");
		memset(block, 0, BLOCK_SIZE);

		ret = i2s_write(dev_i2s, block, BLOCK_SIZE);
		zassert_ok(ret, "i2s_write() failed: %d", ret);
	}

	zassert_ok(i2s_trigger(dev_i2s, I2S_DIR_TX, I2S_TRIGGER_START), "START failed");

	/* Keep the pipe fed so that the device does not run dry mid-test */
	for (int i = 0; i < 4; i++) {
		zassert_ok(k_mem_slab_alloc(&tx_slab, &block, K_NO_WAIT), "slab allocation failed");
		memset(block, 0, BLOCK_SIZE);

		ret = i2s_write(dev_i2s, block, BLOCK_SIZE);
		zassert_ok(ret, "i2s_write() failed: %d", ret);
	}

	zassert_ok(i2s_trigger(dev_i2s, I2S_DIR_TX, I2S_TRIGGER_DRAIN), "DRAIN failed");

	/* All blocks must eventually make it back to the slab */
	for (int i = 0; i < 100 && k_mem_slab_num_free_get(&tx_slab) != BLOCK_COUNT; i++) {
		k_sleep(K_MSEC(10));
	}

	zassert_equal(k_mem_slab_num_free_get(&tx_slab), BLOCK_COUNT,
		      "the driver did not return every transmitted block");
}

ZTEST(i2s_virtio_sound, test_tx_drop_returns_blocks)
{
	struct i2s_config cfg;
	void *block;

	fill_default_config(&cfg, I2S_DIR_TX);
	zassert_ok(i2s_configure(dev_i2s, I2S_DIR_TX, &cfg), "default TX config rejected");

	/* Blocks written before START are only parked, DROP must free them */
	for (int i = 0; i < 2; i++) {
		zassert_ok(k_mem_slab_alloc(&tx_slab, &block, K_NO_WAIT), "slab allocation failed");
		memset(block, 0, BLOCK_SIZE);
		zassert_ok(i2s_write(dev_i2s, block, BLOCK_SIZE), "i2s_write() failed");
	}

	zassert_ok(i2s_trigger(dev_i2s, I2S_DIR_TX, I2S_TRIGGER_DROP), "DROP failed");

	zassert_equal(k_mem_slab_num_free_get(&tx_slab), BLOCK_COUNT,
		      "DROP did not return every queued block");
}

ZTEST(i2s_virtio_sound, test_rx_capture_cycle)
{
	struct i2s_config cfg;
	void *block;
	size_t size;
	int received = 0;
	int ret;

	if (i2s_config_get(dev_i2s, I2S_DIR_RX) == NULL) {
		fill_default_config(&cfg, I2S_DIR_RX);
		ret = i2s_configure(dev_i2s, I2S_DIR_RX, &cfg);
		if (ret == -EINVAL) {
			ztest_test_skip();
		}
		zassert_ok(ret, "default RX config rejected: %d", ret);
	}

	zassert_ok(i2s_trigger(dev_i2s, I2S_DIR_RX, I2S_TRIGGER_START), "RX START failed");

	for (int i = 0; i < 4; i++) {
		ret = i2s_read(dev_i2s, &block, &size);
		if (ret != 0) {
			break;
		}

		zassert_equal(size, BLOCK_SIZE, "unexpected capture block size %zu", size);
		k_mem_slab_free(&rx_slab, block);
		received++;
	}

	zassert_ok(i2s_trigger(dev_i2s, I2S_DIR_RX, I2S_TRIGGER_DROP), "RX DROP failed");

	zassert_true(received > 0, "no capture block was received");
}

ZTEST_SUITE(i2s_virtio_sound, NULL, NULL, NULL, release_all, NULL);
