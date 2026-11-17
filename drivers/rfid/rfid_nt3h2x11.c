/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_nt3h2111

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/rfid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "rfid_nt3h2x11.h"

LOG_MODULE_REGISTER(rfid_nt3h2x11, CONFIG_RFID_LOG_LEVEL);

#define NT3H2X11_POLL_INTERVAL K_MSEC(10)

/* ISO/IEC 14443-3 identification values, fixed by the silicon */
#define NT3H2X11_ATQA_LSB 0x44U
#define NT3H2X11_ATQA_MSB 0x00U
#define NT3H2X11_SAK      0x00U

#define NT3H2X11_MODES                                                                             \
	(RFID_MODE_TARGET | RFID_MODE_TX_RX_SAME_RATE | RFID_MODE_TX_106 | RFID_MODE_RX_106)

struct nt3h2x11_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec fd_gpio;
};

struct nt3h2x11_data {
	struct k_mutex lock;
	struct k_sem fd_sem;
	struct gpio_callback fd_cb;
	uint32_t timeout_us;
};

/*
 * Reads always end the address phase with a STOP: when NFCS_I2C_RST_ON_OFF is
 * set, the tag treats a repeated START as a soft reset of its I2C interface.
 */
static int nt3h2x11_block_read(const struct device *dev, uint8_t block, uint8_t *buf)
{
	const struct nt3h2x11_config *cfg = dev->config;
	int ret;

	ret = i2c_write_dt(&cfg->i2c, &block, sizeof(block));
	if (ret < 0) {
		return ret;
	}

	return i2c_read_dt(&cfg->i2c, buf, NT3H2X11_BLOCK_SIZE);
}

static int nt3h2x11_block_write(const struct device *dev, uint8_t block, const uint8_t *buf)
{
	const struct nt3h2x11_config *cfg = dev->config;
	uint8_t tx[1U + NT3H2X11_BLOCK_SIZE];

	tx[0] = block;
	memcpy(&tx[1], buf, NT3H2X11_BLOCK_SIZE);

	return i2c_write_dt(&cfg->i2c, tx, sizeof(tx));
}

static int nt3h2x11_reg_read(const struct device *dev, uint8_t reg, uint8_t *val)
{
	const struct nt3h2x11_config *cfg = dev->config;
	const uint8_t tx[] = {NT3H2X11_BLOCK_SESSION, reg};
	int ret;

	ret = i2c_write_dt(&cfg->i2c, tx, sizeof(tx));
	if (ret < 0) {
		return ret;
	}

	return i2c_read_dt(&cfg->i2c, val, sizeof(*val));
}

static int nt3h2x11_reg_update(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
	const struct nt3h2x11_config *cfg = dev->config;
	const uint8_t tx[] = {NT3H2X11_BLOCK_SESSION, reg, mask, val};

	return i2c_write_dt(&cfg->i2c, tx, sizeof(tx));
}

static k_timepoint_t nt3h2x11_deadline(const struct device *dev)
{
	struct nt3h2x11_data *data = dev->data;

	if (data->timeout_us == UINT32_MAX) {
		return sys_timepoint_calc(K_FOREVER);
	}

	return sys_timepoint_calc(K_USEC(data->timeout_us));
}

/* Wait until (NS_REG & mask) == match. */
static int nt3h2x11_wait_ns(const struct device *dev, uint8_t mask, uint8_t match, bool need_field,
			    k_timepoint_t end)
{
	const struct nt3h2x11_config *cfg = dev->config;
	struct nt3h2x11_data *data = dev->data;
	uint8_t ns;
	int ret;

	while (true) {
		ret = nt3h2x11_reg_read(dev, NT3H2X11_REG_NS, &ns);
		if (ret < 0) {
			return ret;
		}

		if ((ns & mask) == match) {
			return 0;
		}

		if (need_field && ((ns & NT3H2X11_NS_RF_FIELD_PRESENT) == 0U)) {
			return -ENOTCONN;
		}

		/*
		 * Addressing the tag while its NFC side is idle locks the memory to
		 * I2C until the watchdog expires. Hand it back to the reader.
		 */
		if ((ns & NT3H2X11_NS_I2C_LOCKED) != 0U) {
			ret = nt3h2x11_reg_update(dev, NT3H2X11_REG_NS, NT3H2X11_NS_I2C_LOCKED, 0U);
			if (ret < 0) {
				return ret;
			}
		}

		if (sys_timepoint_expired(end)) {
			return -ETIMEDOUT;
		}

		if (cfg->fd_gpio.port != NULL) {
			(void)k_sem_take(&data->fd_sem, sys_timepoint_timeout(end));
		} else {
			k_sleep(NT3H2X11_POLL_INTERVAL);
		}
	}
}

static int nt3h2x11_pthru_off(const struct device *dev)
{
	/* TRANSFER_DIR stays 1b outside pass-through, or the reader cannot write */
	return nt3h2x11_reg_update(dev, NT3H2X11_REG_NC,
				   NT3H2X11_NC_PTHRU_ON_OFF | NT3H2X11_NC_TRANSFER_DIR |
					   NT3H2X11_NC_FD_OFF | NT3H2X11_NC_FD_ON,
				   NT3H2X11_NC_TRANSFER_DIR | NT3H2X11_NC_FD_FIELD);
}

/*
 * Enable pass-through in the given direction. The direction only changes while
 * pass-through is off, and the tag only enables it while the NFC field is present.
 */
static int nt3h2x11_pthru_on(const struct device *dev, bool nfc_to_i2c)
{
	const uint8_t mask = NT3H2X11_NC_PTHRU_ON_OFF | NT3H2X11_NC_TRANSFER_DIR;
	const uint8_t want =
		NT3H2X11_NC_PTHRU_ON_OFF | (nfc_to_i2c ? NT3H2X11_NC_TRANSFER_DIR : 0U);
	uint8_t nc;
	int ret;

	ret = nt3h2x11_reg_read(dev, NT3H2X11_REG_NC, &nc);
	if (ret < 0) {
		return ret;
	}

	if ((nc & mask) == want) {
		return 0;
	}

	ret = nt3h2x11_reg_update(dev, NT3H2X11_REG_NC, NT3H2X11_NC_PTHRU_ON_OFF, 0U);
	if (ret < 0) {
		return ret;
	}

	ret = nt3h2x11_reg_update(dev, NT3H2X11_REG_NC,
				  NT3H2X11_NC_TRANSFER_DIR | NT3H2X11_NC_FD_OFF | NT3H2X11_NC_FD_ON,
				  (want & NT3H2X11_NC_TRANSFER_DIR) | NT3H2X11_NC_FD_PTHRU);
	if (ret < 0) {
		return ret;
	}

	ret = nt3h2x11_reg_update(dev, NT3H2X11_REG_NC, NT3H2X11_NC_PTHRU_ON_OFF,
				  NT3H2X11_NC_PTHRU_ON_OFF);
	if (ret < 0) {
		return ret;
	}

	ret = nt3h2x11_reg_read(dev, NT3H2X11_REG_NC, &nc);
	if (ret < 0) {
		return ret;
	}

	return ((nc & mask) == want) ? 0 : -ENOTCONN;
}

/* Accessing the last SRAM block hands the buffer over to the other interface. */
static int nt3h2x11_sram_read(const struct device *dev, uint8_t *buf)
{
	int ret;

	for (uint8_t i = 0U; i < NT3H2X11_SRAM_BLOCKS; i++) {
		ret = nt3h2x11_block_read(dev, NT3H2X11_BLOCK_SRAM + i,
					  &buf[i * NT3H2X11_BLOCK_SIZE]);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int nt3h2x11_sram_write(const struct device *dev, const uint8_t *buf)
{
	int ret;

	for (uint8_t i = 0U; i < NT3H2X11_SRAM_BLOCKS; i++) {
		ret = nt3h2x11_block_write(dev, NT3H2X11_BLOCK_SRAM + i,
					   &buf[i * NT3H2X11_BLOCK_SIZE]);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int nt3h2x11_claim(const struct device *dev)
{
	struct nt3h2x11_data *data = dev->data;

	return k_mutex_lock(&data->lock, K_FOREVER);
}

static int nt3h2x11_release(const struct device *dev)
{
	struct nt3h2x11_data *data = dev->data;

	return k_mutex_unlock(&data->lock);
}

static rfid_proto_t nt3h2x11_supported_protocols(const struct device *dev)
{
	ARG_UNUSED(dev);

	return RFID_PROTO_ISO14443A;
}

static rfid_mode_t nt3h2x11_supported_modes(const struct device *dev, rfid_proto_t proto)
{
	ARG_UNUSED(dev);

	return (proto == RFID_PROTO_ISO14443A) ? NT3H2X11_MODES : 0U;
}

static int nt3h2x11_set_protocol(const struct device *dev, rfid_proto_t proto, rfid_mode_t mode)
{
	struct nt3h2x11_data *data = dev->data;
	int ret;

	if ((proto != RFID_PROTO_ISO14443A) || ((mode & ~NT3H2X11_MODES) != 0U) ||
	    ((mode & RFID_MODE_ROLE_MASK) != RFID_MODE_TARGET)) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = nt3h2x11_pthru_off(dev);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int nt3h2x11_get_property(const struct device *dev, struct rfid_property *prop)
{
	struct nt3h2x11_data *data = dev->data;
	uint8_t ns;
	int ret;

	switch (prop->type) {
	case RFID_PROP_RF_FIELD:
		ret = nt3h2x11_reg_read(dev, NT3H2X11_REG_NS, &ns);
		if (ret < 0) {
			return ret;
		}
		prop->rf_on = (ns & NT3H2X11_NS_RF_FIELD_PRESENT) != 0U;
		return 0;
	case RFID_PROP_TIMEOUT:
		prop->timeout_us = data->timeout_us;
		return 0;
	case RFID_PROP_SENS_RES:
		prop->sens_res[0] = NT3H2X11_ATQA_LSB;
		prop->sens_res[1] = NT3H2X11_ATQA_MSB;
		return 0;
	case RFID_PROP_SEL_RES:
		prop->sel_res = NT3H2X11_SAK;
		return 0;
	case RFID_PROP_UID_SIZE:
		prop->uid_size = NT3H2X11_UID_SIZE;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int nt3h2x11_get_properties(const struct device *dev, struct rfid_property *props,
				   size_t props_len)
{
	struct nt3h2x11_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	for (size_t i = 0U; i < props_len; i++) {
		props[i].status = nt3h2x11_get_property(dev, &props[i]);
	}
	k_mutex_unlock(&data->lock);

	return 0;
}

static int nt3h2x11_set_properties(const struct device *dev, struct rfid_property *props,
				   size_t props_len)
{
	struct nt3h2x11_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	for (size_t i = 0U; i < props_len; i++) {
		if (props[i].type == RFID_PROP_TIMEOUT) {
			data->timeout_us = props[i].timeout_us;
			props[i].status = 0;
		} else {
			props[i].status = -ENOTSUP;
		}
	}
	k_mutex_unlock(&data->lock);

	return 0;
}

/*
 * Wait for a reader to enter the field and write a first message into the SRAM.
 * A reader leaving before it wrote anything does not end the wait.
 */
static int nt3h2x11_listen(const struct device *dev, rfid_proto_t proto, uint8_t *rx_data,
			   uint16_t *rx_len)
{
	struct nt3h2x11_data *data = dev->data;
	k_timepoint_t end = nt3h2x11_deadline(dev);
	int ret;

	if (proto != RFID_PROTO_ISO14443A) {
		return -ENOTSUP;
	}

	if (*rx_len < NT3H2X11_SRAM_SIZE) {
		return -ENOSPC;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	do {
		ret = nt3h2x11_pthru_off(dev);
		if (ret < 0) {
			break;
		}

		ret = nt3h2x11_wait_ns(dev, NT3H2X11_NS_RF_FIELD_PRESENT,
				       NT3H2X11_NS_RF_FIELD_PRESENT, false, end);
		if (ret < 0) {
			break;
		}

		ret = nt3h2x11_pthru_on(dev, true);
		if (ret == 0) {
			ret = nt3h2x11_wait_ns(dev, NT3H2X11_NS_SRAM_I2C_READY,
					       NT3H2X11_NS_SRAM_I2C_READY, true, end);
		}
	} while (ret == -ENOTCONN);

	if (ret == 0) {
		ret = nt3h2x11_sram_read(dev, rx_data);
	}

	k_mutex_unlock(&data->lock);

	if (ret == 0) {
		*rx_len = NT3H2X11_SRAM_SIZE;
	}

	return ret;
}

static int nt3h2x11_tm_receive(const struct device *dev, uint8_t *rx_data, uint16_t *rx_len)
{
	struct nt3h2x11_data *data = dev->data;
	k_timepoint_t end = nt3h2x11_deadline(dev);
	int ret;

	if (*rx_len < NT3H2X11_SRAM_SIZE) {
		return -ENOSPC;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = nt3h2x11_pthru_on(dev, true);
	if (ret == 0) {
		ret = nt3h2x11_wait_ns(dev, NT3H2X11_NS_SRAM_I2C_READY, NT3H2X11_NS_SRAM_I2C_READY,
				       true, end);
	}

	if (ret == 0) {
		ret = nt3h2x11_sram_read(dev, rx_data);
	}

	k_mutex_unlock(&data->lock);

	if (ret == 0) {
		*rx_len = NT3H2X11_SRAM_SIZE;
	}

	return ret;
}

/* Returns once the reader has read the whole SRAM back. */
static int nt3h2x11_tm_transmit(const struct device *dev, const uint8_t *tx_data, uint16_t tx_len,
				uint8_t tx_last_bits)
{
	struct nt3h2x11_data *data = dev->data;
	k_timepoint_t end = nt3h2x11_deadline(dev);
	uint8_t buf[NT3H2X11_SRAM_SIZE] = {0};
	int ret;

	if ((tx_last_bits != 0U) && (tx_last_bits != 8U)) {
		return -ENOTSUP;
	}

	if (tx_len > NT3H2X11_SRAM_SIZE) {
		return -EMSGSIZE;
	}

	if (tx_len > 0U) {
		memcpy(buf, tx_data, tx_len);
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = nt3h2x11_pthru_on(dev, false);
	if (ret == 0) {
		ret = nt3h2x11_sram_write(dev, buf);
	}

	if (ret == 0) {
		ret = nt3h2x11_wait_ns(dev, NT3H2X11_NS_SRAM_RF_READY, 0U, true, end);
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static DEVICE_API(rfid, nt3h2x11_api) = {
	.claim = nt3h2x11_claim,
	.release = nt3h2x11_release,
	.set_protocol = nt3h2x11_set_protocol,
	.get_properties = nt3h2x11_get_properties,
	.set_properties = nt3h2x11_set_properties,
	.listen = nt3h2x11_listen,
	.tm_transmit = nt3h2x11_tm_transmit,
	.tm_receive = nt3h2x11_tm_receive,
	.supported_protocols = nt3h2x11_supported_protocols,
	.supported_modes = nt3h2x11_supported_modes,
};

static void nt3h2x11_fd_handler(const struct device *port, struct gpio_callback *cb,
				gpio_port_pins_t pins)
{
	struct nt3h2x11_data *data = CONTAINER_OF(cb, struct nt3h2x11_data, fd_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	k_sem_give(&data->fd_sem);
}

static int nt3h2x11_init_fd(const struct device *dev)
{
	const struct nt3h2x11_config *cfg = dev->config;
	struct nt3h2x11_data *data = dev->data;
	int ret;

	if (!gpio_is_ready_dt(&cfg->fd_gpio)) {
		LOG_ERR("FD GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->fd_gpio, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&data->fd_cb, nt3h2x11_fd_handler, BIT(cfg->fd_gpio.pin));

	ret = gpio_add_callback_dt(&cfg->fd_gpio, &data->fd_cb);
	if (ret < 0) {
		return ret;
	}

	/* Both edges: FD asserts on the awaited event and releases when the field drops */
	return gpio_pin_interrupt_configure_dt(&cfg->fd_gpio, GPIO_INT_EDGE_BOTH);
}

static int nt3h2x11_init(const struct device *dev)
{
	const struct nt3h2x11_config *cfg = dev->config;
	struct nt3h2x11_data *data = dev->data;
	uint8_t ns;
	int ret;

	k_mutex_init(&data->lock);
	k_sem_init(&data->fd_sem, 0, 1);
	/* RFID_PROP_TIMEOUT of UINT32_MAX waits forever */
	data->timeout_us = UINT32_MAX;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	/* Session registers stay readable while a reader holds the memory */
	ret = nt3h2x11_reg_read(dev, NT3H2X11_REG_NS, &ns);
	if (ret < 0) {
		LOG_ERR("Failed to read NS_REG (%d)", ret);
		return ret;
	}

	if (cfg->fd_gpio.port != NULL) {
		ret = nt3h2x11_init_fd(dev);
		if (ret < 0) {
			LOG_ERR("Failed to set up FD GPIO (%d)", ret);
			return ret;
		}
	}

	return 0;
}

#define NT3H2X11_DEFINE(inst)                                                                      \
	static struct nt3h2x11_data nt3h2x11_data_##inst;                                          \
	static const struct nt3h2x11_config nt3h2x11_config_##inst = {                             \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.fd_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, fd_gpios, {0}),                          \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, nt3h2x11_init, NULL, &nt3h2x11_data_##inst,                    \
			      &nt3h2x11_config_##inst, POST_KERNEL, CONFIG_RFID_INIT_PRIORITY,     \
			      &nt3h2x11_api);

DT_INST_FOREACH_STATUS_OKAY(NT3H2X11_DEFINE)
