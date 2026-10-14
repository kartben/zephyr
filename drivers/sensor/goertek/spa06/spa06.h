/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_GOERTEK_SPA06_SPA06_H_
#define ZEPHYR_DRIVERS_SENSOR_GOERTEK_SPA06_SPA06_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/sys/util.h>

#define SPA06_BUS_I2C DT_HAS_COMPAT_ON_BUS_STATUS_OKAY(goertek_spa06, i2c)
#define SPA06_BUS_SPI DT_HAS_COMPAT_ON_BUS_STATUS_OKAY(goertek_spa06, spi)

#define SPA06_REG_PSR_B2   0x00U
#define SPA06_REG_PRS_CFG  0x06U
#define SPA06_REG_TMP_CFG  0x07U
#define SPA06_REG_MEAS_CFG 0x08U
#define SPA06_REG_CFG_REG  0x09U
#define SPA06_REG_INT_STS  0x0AU
#define SPA06_REG_FIFO_STS 0x0BU
#define SPA06_REG_RESET    0x0CU
#define SPA06_REG_ID       0x0DU
#define SPA06_REG_COEF     0x10U

#define SPA06_COEF_LEN 21U

/* PRS_CFG and TMP_CFG */
#define SPA06_CFG_RATE_MASK GENMASK(7, 4)
#define SPA06_CFG_PRC_MASK  GENMASK(3, 0)

/* MEAS_CFG */
#define SPA06_MEAS_CFG_COEF_RDY   BIT(7)
#define SPA06_MEAS_CFG_SENSOR_RDY BIT(6)
#define SPA06_MEAS_CFG_TMP_RDY    BIT(5)
#define SPA06_MEAS_CFG_PRS_RDY    BIT(4)

#define SPA06_MEAS_CTRL_IDLE       0x00U
#define SPA06_MEAS_CTRL_PRS        0x01U
#define SPA06_MEAS_CTRL_TMP        0x02U
#define SPA06_MEAS_CTRL_CONTINUOUS 0x07U

/* CFG_REG */
#define SPA06_CFG_REG_INT_HL   BIT(7)
#define SPA06_CFG_REG_INT_FIFO BIT(6)
#define SPA06_CFG_REG_INT_TMP  BIT(5)
#define SPA06_CFG_REG_INT_PRS  BIT(4)
#define SPA06_CFG_REG_T_SHIFT  BIT(3)
#define SPA06_CFG_REG_P_SHIFT  BIT(2)
#define SPA06_CFG_REG_FIFO_EN  BIT(1)

/* INT_STS */
#define SPA06_INT_STS_FIFO_FULL BIT(2)
#define SPA06_INT_STS_TMP       BIT(1)
#define SPA06_INT_STS_PRS       BIT(0)

/* RESET */
#define SPA06_RESET_FIFO_FLUSH BIT(7)
#define SPA06_RESET_SOFT_RST   0x09U

/* ID */
#define SPA06_ID_PROD_MASK GENMASK(3, 0)
#define SPA06_PROD_ID      0x01U

/* SPI: bit 7 of the address selects a read */
#define SPA06_SPI_READ BIT(7)

/* FIFO depth, and the value read from an empty FIFO */
#define SPA06_FIFO_SIZE  32U
#define SPA06_FIFO_EMPTY 0x800000U
/* The LSB of a FIFO entry is set for pressure and clear for temperature */
#define SPA06_FIFO_PRS   BIT(0)

/* Oversampling index (log2 of the rate) above which results need a bit shift */
#define SPA06_PRC_8X 3U

/* Pressure rate value used for on-demand measurements */
#define SPA06_ODR_NONE 0xFFU

/* Rate code not available for temperature measurements */
#define SPA06_ODR_PRS_ONLY 0x8U

/* Fixed-point formats of the encoded RTIO samples: kPa and degrees Celsius */
#define SPA06_PRESS_SHIFT 8
#define SPA06_TEMP_SHIFT  8

struct spa06_coef {
	int32_t c0;
	int32_t c1;
	int32_t c00;
	int32_t c10;
	int32_t c01;
	int32_t c11;
	int32_t c20;
	int32_t c21;
	int32_t c30;
	int32_t c31;
	int32_t c40;
};

union spa06_bus {
#if SPA06_BUS_I2C
	struct i2c_dt_spec i2c;
#endif
#if SPA06_BUS_SPI
	struct spi_dt_spec spi;
#endif
};

struct spa06_bus_io {
	int (*check)(const union spa06_bus *bus);
	int (*read)(const union spa06_bus *bus, uint8_t reg, uint8_t *buf, size_t len);
	int (*write)(const union spa06_bus *bus, uint8_t reg, uint8_t val);
};

#if SPA06_BUS_I2C
extern const struct spa06_bus_io spa06_bus_io_i2c;
#endif

#if SPA06_BUS_SPI
#define SPA06_SPI_OPERATION (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA)

extern const struct spa06_bus_io spa06_bus_io_spi;
#endif

struct spa06_config {
	union spa06_bus bus;
	const struct spa06_bus_io *bus_io;
#ifdef CONFIG_SPA06_TRIGGER
	struct gpio_dt_spec int_gpio;
#endif
	uint8_t prs_prc;
	uint8_t tmp_prc;
	uint8_t prs_odr;
	uint8_t tmp_odr;
};

struct spa06_data {
	struct spa06_coef coef;

	/* Latest results, with the oversampling they were measured with */
	int32_t raw_prs;
	int32_t raw_tmp;
	uint8_t raw_prs_prc;
	uint8_t raw_tmp_prc;

	/* Current oversampling and rate settings */
	uint8_t prs_prc;
	uint8_t tmp_prc;
	uint8_t prs_odr;
	uint8_t tmp_odr;

	/* Interrupt and FIFO enable bits of CFG_REG */
	uint8_t int_cfg;

	/* Continuous measurements have produced pressure and temperature results */
	bool have_results;

#ifdef CONFIG_SPA06_TRIGGER
	const struct device *dev;
	struct gpio_callback gpio_cb;
	sensor_trigger_handler_t drdy_handler;
	const struct sensor_trigger *drdy_trigger;
#if defined(CONFIG_SPA06_TRIGGER_OWN_THREAD)
	K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_SPA06_THREAD_STACK_SIZE);
	struct k_thread thread;
	struct k_sem gpio_sem;
#elif defined(CONFIG_SPA06_TRIGGER_GLOBAL_THREAD)
	struct k_work work;
#endif
#endif /* CONFIG_SPA06_TRIGGER */

#ifdef CONFIG_SPA06_STREAM
	atomic_ptr_t stream_sqe;
	uint8_t stream_events;
#endif
};

/* Events that complete a stream request */
#define SPA06_EVENT_DRDY      BIT(0)
#define SPA06_EVENT_FIFO_FULL BIT(1)

/* Encoded RTIO buffer: pressure samples, then temperature samples, in q31 */
struct spa06_encoded_header {
	uint64_t timestamp;
	uint32_t prs_period_ns;
	uint32_t tmp_period_ns;
	uint8_t events;
	uint8_t prs_count;
	uint8_t tmp_count;
};

struct spa06_encoded_data {
	struct spa06_encoded_header header;
	int32_t samples[];
};

/* Whether the device is resumed; without device power management it always is */
static inline bool spa06_is_active(const struct device *dev)
{
	enum pm_device_state state;

	(void)pm_device_state_get(dev, &state);

	return state == PM_DEVICE_STATE_ACTIVE;
}

int spa06_reg_read(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len);
int spa06_reg_write(const struct device *dev, uint8_t reg, uint8_t val);
int spa06_write_cfg_reg(const struct device *dev);

int spa06_sample_fetch(const struct device *dev, enum sensor_channel chan);

int64_t spa06_comp_press_micro(const struct spa06_coef *coef, int32_t raw_prs, uint8_t prs_prc,
			       int32_t raw_tmp, uint8_t tmp_prc);
int64_t spa06_comp_temp_micro(const struct spa06_coef *coef, int32_t raw_tmp, uint8_t tmp_prc);
int32_t spa06_micro_to_q31(int64_t micro, int8_t shift);
uint32_t spa06_odr_period_ns(uint8_t odr);

#ifdef CONFIG_SPA06_TRIGGER
int spa06_trigger_init(const struct device *dev);
int spa06_trigger_enable(const struct device *dev, bool enable);
int spa06_trigger_set(const struct device *dev, const struct sensor_trigger *trig,
		      sensor_trigger_handler_t handler);
#endif

#ifdef CONFIG_SENSOR_ASYNC_API
void spa06_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe);
int spa06_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder);
#endif

#ifdef CONFIG_SPA06_STREAM
void spa06_stream_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe);
bool spa06_stream_handle_interrupt(const struct device *dev);
#endif

#endif /* ZEPHYR_DRIVERS_SENSOR_GOERTEK_SPA06_SPA06_H_ */
