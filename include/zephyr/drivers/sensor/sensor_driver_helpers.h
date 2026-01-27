/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_DRIVER_HELPERS_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_DRIVER_HELPERS_H_

/**
 * @file
 * @brief Sensor driver helper macros
 *
 * This file provides helper macros to reduce boilerplate code in sensor drivers
 * while maintaining readability and consistency.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device.h>
#include <zephyr/logging/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sensor_driver_helpers Sensor Driver Helper Macros
 * @ingroup sensor_interface
 * @{
 */

/* ============================================================================
 * I2C/SPI Register Access Helpers
 * ============================================================================
 */

/**
 * @brief Define I2C register read/write helper functions
 *
 * Generates inline helper functions for I2C register access that retrieve
 * the I2C spec from the device config structure.
 *
 * @param prefix Driver-specific function name prefix (e.g., lm75)
 * @param config_type Name of the driver's config structure type
 * @param i2c_field Name of the I2C DT spec field in the config structure
 *
 * Example usage:
 * @code{.c}
 * SENSOR_I2C_REG_ACCESS(lm75, lm75_config, i2c)
 * // Generates: lm75_reg_read() and lm75_reg_write()
 * @endcode
 */
#define SENSOR_I2C_REG_ACCESS(prefix, config_type, i2c_field)                                      \
	static inline int prefix##_reg_read(const struct device *dev, uint8_t reg, uint8_t *buf,   \
					    uint32_t size)                                         \
	{                                                                                          \
		const struct config_type *cfg = dev->config;                                       \
                                                                                                   \
		return i2c_burst_read_dt(&cfg->i2c_field, reg, buf, size);                         \
	}                                                                                          \
                                                                                                   \
	static inline int prefix##_reg_write(const struct device *dev, uint8_t reg,                \
					     const uint8_t *buf, uint32_t size)                    \
	{                                                                                          \
		const struct config_type *cfg = dev->config;                                       \
                                                                                                   \
		return i2c_burst_write_dt(&cfg->i2c_field, reg, buf, size);                        \
	}

/**
 * @brief Define SPI register read/write helper functions
 *
 * Generates inline helper functions for SPI register access that retrieve
 * the SPI spec from the device config structure.
 *
 * @param prefix Driver-specific function name prefix
 * @param config_type Name of the driver's config structure type
 * @param spi_field Name of the SPI DT spec field in the config structure
 *
 * Example usage:
 * @code{.c}
 * SENSOR_SPI_REG_ACCESS(bme280, bme280_config, spi)
 * // Generates: bme280_reg_read() and bme280_reg_write()
 * @endcode
 */
#define SENSOR_SPI_REG_ACCESS(prefix, config_type, spi_field)                                      \
	static inline int prefix##_reg_read(const struct device *dev, uint8_t reg, uint8_t *buf,   \
					    uint32_t size)                                         \
	{                                                                                          \
		const struct config_type *cfg = dev->config;                                       \
		uint8_t tx_buf[2] = {reg | 0x80, 0};                                               \
		const struct spi_buf tx = {.buf = tx_buf, .len = 2};                               \
		const struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};                    \
		struct spi_buf rx[2] = {{.buf = NULL, .len = 1}, {.buf = buf, .len = size}};       \
		const struct spi_buf_set rx_set = {.buffers = rx, .count = 2};                     \
                                                                                                   \
		return spi_transceive_dt(&cfg->spi_field, &tx_set, &rx_set);                       \
	}                                                                                          \
                                                                                                   \
	static inline int prefix##_reg_write(const struct device *dev, uint8_t reg,                \
					     const uint8_t *buf, uint32_t size)                    \
	{                                                                                          \
		const struct config_type *cfg = dev->config;                                       \
		uint8_t tx_buf[size + 1];                                                          \
		tx_buf[0] = reg & 0x7F;                                                            \
		memcpy(&tx_buf[1], buf, size);                                                     \
		const struct spi_buf tx = {.buf = tx_buf, .len = size + 1};                        \
		const struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};                    \
                                                                                                   \
		return spi_write_dt(&cfg->spi_field, &tx_set);                                     \
	}

/* ============================================================================
 * Device Initialization Helpers
 * ============================================================================
 */

/**
 * @brief Check if I2C bus is ready during initialization
 *
 * Checks if the I2C bus specified in the device config is ready.
 * Logs an error and returns -ENODEV if not ready.
 *
 * @param dev Pointer to the device structure
 * @param cfg Pointer to the device config structure
 * @param i2c_field Name of the I2C DT spec field in the config structure
 *
 * Example usage:
 * @code{.c}
 * static int lm75_init(const struct device *dev)
 * {
 *     const struct lm75_config *cfg = dev->config;
 *     SENSOR_DEVICE_I2C_BUS_READY_CHECK(dev, cfg, i2c);
 *     // ... rest of init code
 * }
 * @endcode
 */
#define SENSOR_DEVICE_I2C_BUS_READY_CHECK(dev, cfg, i2c_field)                                     \
	do {                                                                                       \
		if (!device_is_ready((cfg)->i2c_field.bus)) {                                      \
			LOG_ERR("I2C bus not ready");                                              \
			return -ENODEV;                                                            \
		}                                                                                  \
	} while (0)

/**
 * @brief Check if SPI bus is ready during initialization
 *
 * Checks if the SPI bus specified in the device config is ready.
 * Logs an error and returns -ENODEV if not ready.
 *
 * @param dev Pointer to the device structure
 * @param cfg Pointer to the device config structure
 * @param spi_field Name of the SPI DT spec field in the config structure
 */
#define SENSOR_DEVICE_SPI_BUS_READY_CHECK(dev, cfg, spi_field)                                     \
	do {                                                                                       \
		if (!device_is_ready((cfg)->spi_field.bus)) {                                      \
			LOG_ERR("SPI bus not ready");                                              \
			return -ENODEV;                                                            \
		}                                                                                  \
	} while (0)

/**
 * @brief Initialize runtime power management for a sensor device
 *
 * Initializes the device as suspended and enables runtime power management.
 * Only active when CONFIG_PM_DEVICE_RUNTIME is enabled.
 *
 * @param dev Pointer to the device structure
 *
 * Example usage:
 * @code{.c}
 * static int lm75_init(const struct device *dev)
 * {
 *     // ... bus ready check
 *     SENSOR_DEVICE_PM_INIT(dev);
 *     // ... rest of init code
 * }
 * @endcode
 */
#define SENSOR_DEVICE_PM_INIT(dev)                                                                 \
	do {                                                                                       \
		IF_ENABLED(CONFIG_PM_DEVICE_RUNTIME, (				\
			pm_device_init_suspended(dev);				\
			int _ret = pm_device_runtime_enable(dev);		\
			if (_ret < 0 && _ret != -ENOTSUP) {			\
				LOG_ERR("Failed to enable runtime PM");	\
				return _ret;					\
			}							\
		))                                        \
	} while (0)

/* ============================================================================
 * Channel Dispatch Helpers
 * ============================================================================
 */

/*
 * Note: Complex variadic macro-based dispatch helpers have been omitted
 * from this initial implementation to maintain compatibility and simplicity.
 * Drivers should implement sample_fetch and channel_get functions manually
 * using standard switch statements, as shown in the example driver.
 */

/* ============================================================================
 * Power Management Helpers
 * ============================================================================
 */

/**
 * @brief Define a no-op power management action function
 *
 * Creates a basic power management function that accepts all standard
 * PM actions but performs no operations. Useful for simple sensors that
 * don't require custom power management logic.
 *
 * @param prefix Driver-specific function name prefix
 *
 * Example usage:
 * @code{.c}
 * SENSOR_PM_ACTION_NOOP(lm75)
 * // Generates: lm75_pm_action() that returns 0 for standard actions
 * @endcode
 */
#define SENSOR_PM_ACTION_NOOP(prefix)                                                              \
	IF_ENABLED(CONFIG_PM_DEVICE, (						\
	static int prefix##_pm_action(const struct device *dev,		\
				      enum pm_device_action action)		\
	{									\
		ARG_UNUSED(dev);						\
										\
		switch (action) {						\
		case PM_DEVICE_ACTION_TURN_ON:					\
		case PM_DEVICE_ACTION_RESUME:					\
		case PM_DEVICE_ACTION_TURN_OFF:					\
		case PM_DEVICE_ACTION_SUSPEND:					\
			break;							\
		default:							\
			return -ENOTSUP;					\
		}								\
		return 0;							\
	}									\
	))

/* ============================================================================
 * Trigger Handling Helpers
 * ============================================================================
 */

/**
 * @brief Define GPIO trigger initialization boilerplate
 *
 * Generates code to initialize a GPIO trigger during device initialization.
 * This includes configuring the GPIO pin, setting up the callback, and
 * initializing the work queue.
 *
 * @param data Variable name of the driver data structure
 * @param cfg Variable name of the driver config structure
 * @param gpio_field Name of the GPIO DT spec field in config
 * @param callback_func Name of the GPIO callback function
 * @param work_func Name of the work handler function
 * @param workq_field Name of the work queue field in data structure
 * @param stack_field Name of the stack field in data structure
 * @param stack_size_config Name of the Kconfig option for stack size
 * @param prio_config Name of the Kconfig option for thread priority
 * @param thread_name String name for the thread
 *
 * Example usage:
 * @code{.c}
 * #if defined(CONFIG_LM75_TRIGGER)
 * static int lm75_init(const struct device *dev)
 * {
 *     // ... other init code
 *     SENSOR_TRIGGER_GPIO_INIT(data, cfg, int_gpio,
 *                              lm75_gpio_callback,
 *                              lm75_work_handler,
 *                              workq, stack,
 *                              CONFIG_LM75_TRIGGER_THREAD_STACK_SIZE,
 *                              CONFIG_LM75_TRIGGER_THREAD_PRIO,
 *                              "lm75_trigger");
 *     return 0;
 * }
 * #endif
 * @endcode
 */
#define SENSOR_TRIGGER_GPIO_INIT(data, cfg, gpio_field, callback_func, work_func, workq_field,     \
				 stack_field, stack_size_config, prio_config, thread_name)         \
	do {                                                                                       \
		if ((cfg)->gpio_field.port != NULL) {                                              \
			(data)->dev = dev;                                                         \
			k_work_queue_start(&(data)->workq_field, (data)->stack_field,              \
					   K_THREAD_STACK_SIZEOF((data)->stack_field),             \
					   prio_config, NULL);                                     \
			k_thread_name_set(&(data)->workq_field.thread, thread_name);               \
			k_work_init(&(data)->work, work_func);                                     \
                                                                                                   \
			if (!device_is_ready((cfg)->gpio_field.port)) {                            \
				LOG_ERR("GPIO not ready");                                         \
				return -ENODEV;                                                    \
			}                                                                          \
                                                                                                   \
			int _ret = gpio_pin_configure_dt(&(cfg)->gpio_field, GPIO_INPUT);          \
			if (_ret < 0) {                                                            \
				LOG_ERR("Failed to configure GPIO");                               \
				return _ret;                                                       \
			}                                                                          \
                                                                                                   \
			gpio_init_callback(&(data)->gpio_cb, callback_func,                        \
					   BIT((cfg)->gpio_field.pin));                            \
                                                                                                   \
			_ret = gpio_add_callback((cfg)->gpio_field.port, &(data)->gpio_cb);        \
			if (_ret < 0) {                                                            \
				LOG_ERR("Failed to add GPIO callback");                            \
				return _ret;                                                       \
			}                                                                          \
		}                                                                                  \
	} while (0)

/**
 * @brief Define standard trigger work handler
 *
 * Generates a work handler function that calls the user's trigger handler
 * if one has been set.
 *
 * @param prefix Driver-specific function name prefix
 * @param data_type Name of the driver's data structure type
 *
 * Example usage:
 * @code{.c}
 * SENSOR_TRIGGER_WORK_HANDLER(lm75, lm75_data)
 * // Generates: lm75_trigger_work_handler()
 * @endcode
 */
#define SENSOR_TRIGGER_WORK_HANDLER(prefix, data_type)                                             \
	static void prefix##_trigger_work_handler(struct k_work *item)                             \
	{                                                                                          \
		struct data_type *data = CONTAINER_OF(item, struct data_type, work);               \
                                                                                                   \
		if (data->trigger_handler != NULL) {                                               \
			data->trigger_handler(data->dev, (struct sensor_trigger *)data->trigger);  \
		}                                                                                  \
	}

/**
 * @brief Define standard GPIO callback for triggers
 *
 * Generates a GPIO callback function that submits work to the work queue.
 *
 * @param prefix Driver-specific function name prefix
 * @param data_type Name of the driver's data structure type
 *
 * Example usage:
 * @code{.c}
 * SENSOR_TRIGGER_GPIO_CALLBACK(lm75, lm75_data)
 * // Generates: lm75_gpio_callback()
 * @endcode
 */
#define SENSOR_TRIGGER_GPIO_CALLBACK(prefix, data_type)                                            \
	static void prefix##_gpio_callback(const struct device *port, struct gpio_callback *cb,    \
					   gpio_port_pins_t pins)                                  \
	{                                                                                          \
		struct data_type *data = CONTAINER_OF(cb, struct data_type, gpio_cb);              \
                                                                                                   \
		ARG_UNUSED(port);                                                                  \
		ARG_UNUSED(pins);                                                                  \
                                                                                                   \
		k_work_submit_to_queue(&data->workq, &data->work);                                 \
	}

/* ============================================================================
 * Device Tree Instantiation Helpers
 * ============================================================================
 */

/**
 * @brief Define device instances for an I2C sensor
 *
 * Generates all necessary structures and device definitions for an I2C-based
 * sensor driver. This macro significantly reduces boilerplate by generating:
 * - Data structure instance
 * - Config structure instance with I2C spec
 * - Power management device (if applicable)
 * - Sensor device definition
 *
 * @param prefix Driver-specific prefix for naming
 * @param inst Device tree instance number
 * @param data_type Name of the driver's data structure type
 * @param config_type Name of the driver's config structure type
 * @param init_func Name of the driver's init function
 * @param pm_func Name of the PM action function (or NULL)
 * @param api_struct Name of the driver API structure
 * @param ... Additional config structure initializers (optional)
 *
 * Example usage:
 * @code{.c}
 * #define LM75_INST(inst) \
 *     SENSOR_DEVICE_DT_INST_I2C_DEFINE(lm75, inst, \
 *         lm75_data, lm75_config, \
 *         lm75_init, lm75_pm_action, lm75_driver_api, \
 *         .config_reg = { .shutdown = 0 })
 *
 * DT_INST_FOREACH_STATUS_OKAY(LM75_INST)
 * @endcode
 */
#define SENSOR_DEVICE_DT_INST_I2C_DEFINE(prefix, inst, data_type, config_type, init_func, pm_func, \
					 api_struct, ...)                                          \
	static struct data_type prefix##_data_##inst;                                              \
	static const struct config_type prefix##_config_##inst = {                                 \
		.i2c = I2C_DT_SPEC_INST_GET(inst), __VA_ARGS__};                                   \
	PM_DEVICE_DT_INST_DEFINE(inst, pm_func);                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, init_func, PM_DEVICE_DT_INST_GET(inst),                 \
				     &prefix##_data_##inst, &prefix##_config_##inst, POST_KERNEL,  \
				     CONFIG_SENSOR_INIT_PRIORITY, &api_struct)

/**
 * @brief Define device instances for an SPI sensor
 *
 * Similar to SENSOR_DEVICE_DT_INST_I2C_DEFINE but for SPI-based sensors.
 *
 * @param prefix Driver-specific prefix for naming
 * @param inst Device tree instance number
 * @param data_type Name of the driver's data structure type
 * @param config_type Name of the driver's config structure type
 * @param init_func Name of the driver's init function
 * @param pm_func Name of the PM action function (or NULL)
 * @param api_struct Name of the driver API structure
 * @param ... Additional config structure initializers (optional)
 */
#define SENSOR_DEVICE_DT_INST_SPI_DEFINE(prefix, inst, data_type, config_type, init_func, pm_func, \
					 api_struct, ...)                                          \
	static struct data_type prefix##_data_##inst;                                              \
	static const struct config_type prefix##_config_##inst = {                                 \
		.spi = SPI_DT_SPEC_INST_GET(                                                       \
			inst, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8), 0),         \
		__VA_ARGS__};                                                                      \
	PM_DEVICE_DT_INST_DEFINE(inst, pm_func);                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, init_func, PM_DEVICE_DT_INST_GET(inst),                 \
				     &prefix##_data_##inst, &prefix##_config_##inst, POST_KERNEL,  \
				     CONFIG_SENSOR_INIT_PRIORITY, &api_struct)

/**
 * @brief Define device instances for an I2C sensor with GPIO trigger support
 *
 * Similar to SENSOR_DEVICE_DT_INST_I2C_DEFINE but adds GPIO interrupt
 * pin configuration for sensors that support triggers.
 *
 * @param prefix Driver-specific prefix for naming
 * @param inst Device tree instance number
 * @param data_type Name of the driver's data structure type
 * @param config_type Name of the driver's config structure type
 * @param init_func Name of the driver's init function
 * @param pm_func Name of the PM action function (or NULL)
 * @param api_struct Name of the driver API structure
 * @param gpio_prop Device tree GPIO property name (e.g., "int_gpios")
 * @param ... Additional config structure initializers (optional)
 */
#define SENSOR_DEVICE_DT_INST_I2C_DEFINE_WITH_TRIGGER(                                             \
	prefix, inst, data_type, config_type, init_func, pm_func, api_struct, gpio_prop, ...)      \
	static struct data_type prefix##_data_##inst;                                              \
	static const struct config_type prefix##_config_##inst = {                                 \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, gpio_prop, {0}),                        \
		__VA_ARGS__};                                                                      \
	PM_DEVICE_DT_INST_DEFINE(inst, pm_func);                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, init_func, PM_DEVICE_DT_INST_GET(inst),                 \
				     &prefix##_data_##inst, &prefix##_config_##inst, POST_KERNEL,  \
				     CONFIG_SENSOR_INIT_PRIORITY, &api_struct)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_DRIVER_HELPERS_H_ */
