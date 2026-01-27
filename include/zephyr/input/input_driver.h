/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Helper macros for input drivers
 * @ingroup input_interface
 */

#ifndef ZEPHYR_INCLUDE_INPUT_DRIVER_H_
#define ZEPHYR_INCLUDE_INPUT_DRIVER_H_

/**
 * @brief Input Driver Helper Macros
 * @defgroup input_driver_macros Input Driver Helper Macros
 * @ingroup input_interface
 * @{
 *
 * This header provides helper macros to reduce boilerplate code in input
 * drivers while maintaining readability. These macros encapsulate common
 * patterns found across multiple input drivers.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if an I2C bus is ready and log an error if not
 *
 * This macro checks if an I2C bus specified in a dt_spec is ready for use.
 * If not ready, it logs an error and returns -ENODEV.
 *
 * @param _i2c_spec Pointer to struct i2c_dt_spec
 *
 * Example usage:
 * @code{.c}
 * static int my_driver_init(const struct device *dev)
 * {
 *     const struct my_driver_config *cfg = dev->config;
 *     INPUT_I2C_CHECK_READY(&cfg->i2c);
 *     // ... rest of init
 * }
 * @endcode
 */
#define INPUT_I2C_CHECK_READY(_i2c_spec)                                       \
	do {                                                                   \
		if (!device_is_ready((_i2c_spec)->bus)) {                      \
			LOG_ERR("I2C controller device not ready");            \
			return -ENODEV;                                        \
		}                                                              \
	} while (0)

/**
 * @brief Check if an SPI bus is ready and log an error if not
 *
 * This macro checks if an SPI bus specified in a dt_spec is ready for use.
 * If not ready, it logs an error and returns -ENODEV.
 *
 * @param _spi_spec Pointer to struct spi_dt_spec
 *
 * Example usage:
 * @code{.c}
 * static int my_driver_init(const struct device *dev)
 * {
 *     const struct my_driver_config *cfg = dev->config;
 *     INPUT_SPI_CHECK_READY(&cfg->spi);
 *     // ... rest of init
 * }
 * @endcode
 */
#define INPUT_SPI_CHECK_READY(_spi_spec)                                       \
	do {                                                                   \
		if (!spi_is_ready_dt(_spi_spec)) {                            \
			LOG_ERR("%s is not ready", (_spi_spec)->bus->name);    \
			return -ENODEV;                                        \
		}                                                              \
	} while (0)

/**
 * @brief Check if a GPIO device is ready and log an error if not
 *
 * This macro checks if a GPIO device specified in a dt_spec is ready for use.
 * If not ready, it logs an error and returns -ENODEV.
 *
 * @param _gpio_spec Pointer to struct gpio_dt_spec
 *
 * Example usage:
 * @code{.c}
 * static int my_driver_init(const struct device *dev)
 * {
 *     const struct my_driver_config *cfg = dev->config;
 *     INPUT_GPIO_CHECK_READY(&cfg->int_gpio);
 *     // ... rest of init
 * }
 * @endcode
 */
#define INPUT_GPIO_CHECK_READY(_gpio_spec)                                     \
	do {                                                                   \
		if (!gpio_is_ready_dt(_gpio_spec)) {                          \
			LOG_ERR("%s is not ready", (_gpio_spec)->port->name);  \
			return -ENODEV;                                        \
		}                                                              \
	} while (0)

/**
 * @brief Configure GPIO pin and log error on failure
 *
 * This macro configures a GPIO pin and checks the return value, logging an
 * error and returning the error code if configuration fails.
 *
 * @param _gpio_spec Pointer to struct gpio_dt_spec
 * @param _flags GPIO configuration flags (e.g., GPIO_INPUT, GPIO_OUTPUT_ACTIVE)
 *
 * Example usage:
 * @code{.c}
 * INPUT_GPIO_PIN_CONFIGURE(&cfg->reset_gpio, GPIO_OUTPUT_ACTIVE);
 * @endcode
 */
#define INPUT_GPIO_PIN_CONFIGURE(_gpio_spec, _flags)                           \
	do {                                                                   \
		int _ret = gpio_pin_configure_dt(_gpio_spec, _flags);         \
		if (_ret != 0) {                                               \
			LOG_ERR("GPIO pin configuration failed: %d", _ret);    \
			return _ret;                                           \
		}                                                              \
	} while (0)

/**
 * @brief Setup GPIO interrupt with standard configuration
 *
 * This macro configures a GPIO pin as input, initializes a callback, adds
 * the callback to the GPIO port, and enables the interrupt. This is a very
 * common pattern in input drivers that use GPIO interrupts.
 *
 * @param _gpio_spec Pointer to struct gpio_dt_spec
 * @param _cb_data Pointer to struct gpio_callback to be initialized
 * @param _handler GPIO callback handler function
 * @param _mode Interrupt mode (e.g., GPIO_INT_EDGE_TO_ACTIVE, GPIO_INT_EDGE_BOTH)
 *
 * Example usage:
 * @code{.c}
 * struct my_driver_data {
 *     struct gpio_callback int_gpio_cb;  // Callback structure
 *     struct k_work work;
 * };
 *
 * static void my_gpio_handler(const struct device *port,
 *                             struct gpio_callback *cb, uint32_t pins) {
 *     struct my_driver_data *data = CONTAINER_OF(cb,
 *                                                 struct my_driver_data,
 *                                                 int_gpio_cb);
 *     // Handle interrupt - typically submit work to work queue
 * }
 *
 * static int my_driver_init(const struct device *dev) {
 *     const struct my_driver_config *cfg = dev->config;
 *     struct my_driver_data *data = dev->data;
 *
 *     // Initialize the gpio_callback structure and setup interrupt
 *     INPUT_GPIO_INTERRUPT_INIT(&cfg->int_gpio, &data->int_gpio_cb,
 *                               my_gpio_handler, GPIO_INT_EDGE_TO_ACTIVE);
 *     // ... rest of init
 * }
 * @endcode
 */
#define INPUT_GPIO_INTERRUPT_INIT(_gpio_spec, _cb_data, _handler, _mode)       \
	do {                                                                   \
		int _ret;                                                      \
		INPUT_GPIO_CHECK_READY(_gpio_spec);                           \
		INPUT_GPIO_PIN_CONFIGURE(_gpio_spec, GPIO_INPUT);             \
		gpio_init_callback(_cb_data, _handler,                        \
				   BIT((_gpio_spec)->pin));                    \
		_ret = gpio_add_callback_dt(_gpio_spec, _cb_data);            \
		if (_ret < 0) {                                                \
			LOG_ERR("Could not set gpio callback: %d", _ret);     \
			return _ret;                                           \
		}                                                              \
		_ret = gpio_pin_interrupt_configure_dt(_gpio_spec, _mode);    \
		if (_ret < 0) {                                                \
			LOG_ERR("GPIO interrupt configuration failed: %d",    \
				_ret);                                         \
			return _ret;                                           \
		}                                                              \
	} while (0)

/**
 * @brief Initialize work queue with handler
 *
 * This macro initializes a k_work structure with the specified handler.
 * This is a simple wrapper but improves consistency and readability.
 *
 * @param _work Pointer to struct k_work
 * @param _handler Work handler function
 *
 * Example usage:
 * @code{.c}
 * static void my_work_handler(struct k_work *work) {
 *     // handle work
 * }
 *
 * static int my_driver_init(const struct device *dev) {
 *     struct my_driver_data *data = dev->data;
 *     INPUT_WORK_INIT(&data->work, my_work_handler);
 *     // ... rest of init
 * }
 * @endcode
 */
#define INPUT_WORK_INIT(_work, _handler)                                       \
	k_work_init(_work, _handler)

/**
 * @brief Initialize delayed work queue with handler
 *
 * This macro initializes a k_work_delayable structure with the specified
 * handler. This is used for debouncing and delayed processing.
 *
 * @param _work Pointer to struct k_work_delayable
 * @param _handler Work handler function
 *
 * Example usage:
 * @code{.c}
 * static void my_delayed_work_handler(struct k_work *work) {
 *     // handle delayed work
 * }
 *
 * static int my_driver_init(const struct device *dev) {
 *     struct my_driver_data *data = dev->data;
 *     INPUT_WORK_DELAYABLE_INIT(&data->dwork, my_delayed_work_handler);
 *     // ... rest of init
 * }
 * @endcode
 */
#define INPUT_WORK_DELAYABLE_INIT(_work, _handler)                             \
	k_work_init_delayable(_work, _handler)

/**
 * @brief Initialize a polling timer
 *
 * This macro initializes a k_timer for polling mode operation and starts
 * it with the specified period. Common in drivers that support polling mode.
 *
 * @param _timer Pointer to struct k_timer
 * @param _handler Timer expiry handler function
 * @param _period_ms Polling period in milliseconds
 *
 * Example usage:
 * @code{.c}
 * static void my_timer_handler(struct k_timer *timer) {
 *     // handle timer expiry
 * }
 *
 * static int my_driver_init(const struct device *dev) {
 *     const struct my_driver_config *cfg = dev->config;
 *     struct my_driver_data *data = dev->data;
 *     INPUT_POLL_TIMER_INIT(&data->timer, my_timer_handler,
 *                           cfg->poll_interval_ms);
 *     // ... rest of init
 * }
 * @endcode
 */
#define INPUT_POLL_TIMER_INIT(_timer, _handler, _period_ms)                    \
	do {                                                                   \
		k_timer_init(_timer, _handler, NULL);                         \
		k_timer_start(_timer, K_MSEC(_period_ms),                     \
			      K_MSEC(_period_ms));                             \
	} while (0)

/**
 * @brief Submit work from interrupt context
 *
 * This macro is a simple wrapper around k_work_submit but makes the intent
 * clearer when called from interrupt handlers. It's commonly used in GPIO
 * interrupt callbacks to defer processing to a work queue.
 *
 * @param _work Pointer to struct k_work
 *
 * Example usage:
 * @code{.c}
 * static void my_gpio_isr(const struct device *port,
 *                         struct gpio_callback *cb, uint32_t pins) {
 *     struct my_driver_data *data = CONTAINER_OF(cb, struct my_driver_data, cb);
 *     INPUT_WORK_SUBMIT(&data->work);
 * }
 * @endcode
 */
#define INPUT_WORK_SUBMIT(_work)                                               \
	k_work_submit(_work)

/**
 * @brief Reschedule delayed work from interrupt context
 *
 * This macro reschedules delayed work, commonly used for debouncing in
 * GPIO interrupt handlers.
 *
 * @param _work Pointer to struct k_work_delayable
 * @param _delay Delay in milliseconds before work execution
 *
 * Example usage:
 * @code{.c}
 * static void my_gpio_isr(const struct device *port,
 *                         struct gpio_callback *cb, uint32_t pins) {
 *     struct my_driver_data *data = CONTAINER_OF(cb, struct my_driver_data, cb);
 *     INPUT_WORK_RESCHEDULE(&data->dwork, CONFIG_MY_DRIVER_DEBOUNCE_MS);
 * }
 * @endcode
 */
#define INPUT_WORK_RESCHEDULE(_work, _delay_ms)                                \
	k_work_reschedule(_work, K_MSEC(_delay_ms))

/**
 * @brief Common input driver initialization pattern
 *
 * This macro combines several common initialization steps:
 * - Store device pointer in driver data
 * - Initialize work queue
 * - Check and configure interrupt GPIO (if provided)
 * - Setup GPIO callback and interrupt
 *
 * This significantly reduces boilerplate in simple interrupt-driven drivers.
 *
 * @param _dev Device pointer
 * @param _data Pointer to driver data structure
 * @param _work Work queue member name in data structure
 * @param _work_handler Work handler function
 * @param _gpio_cb GPIO callback member name in data structure
 * @param _gpio_handler GPIO interrupt handler function
 * @param _gpio_spec Pointer to GPIO dt_spec (can be NULL for polling mode)
 * @param _int_mode Interrupt mode (e.g., GPIO_INT_EDGE_TO_ACTIVE)
 *
 * Example usage:
 * @code{.c}
 * static int my_driver_init(const struct device *dev) {
 *     const struct my_driver_config *cfg = dev->config;
 *     struct my_driver_data *data = dev->data;
 *
 *     INPUT_COMMON_INIT_INTERRUPT(dev, data, work, my_work_handler,
 *                                 int_gpio_cb, my_gpio_handler,
 *                                 &cfg->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
 *     // ... driver-specific init
 *     return 0;
 * }
 * @endcode
 */
#define INPUT_COMMON_INIT_INTERRUPT(_dev, _data, _work, _work_handler,        \
				    _gpio_cb, _gpio_handler, _gpio_spec,      \
				    _int_mode)                                 \
	do {                                                                   \
		(_data)->dev = (_dev);                                         \
		INPUT_WORK_INIT(&(_data)->_work, _work_handler);              \
		if (_gpio_spec != NULL) {                                      \
			INPUT_GPIO_INTERRUPT_INIT(_gpio_spec,                 \
						  &(_data)->_gpio_cb,          \
						  _gpio_handler, _int_mode);   \
		}                                                              \
	} while (0)

/**
 * @brief Common input driver initialization for polling mode
 *
 * This macro combines several common initialization steps for polling mode:
 * - Store device pointer in driver data
 * - Initialize work queue
 * - Initialize and start polling timer
 *
 * @param _dev Device pointer
 * @param _data Pointer to driver data structure
 * @param _work Work queue member name in data structure
 * @param _work_handler Work handler function
 * @param _timer Timer member name in data structure
 * @param _timer_handler Timer handler function
 * @param _period_ms Polling period in milliseconds
 *
 * Example usage:
 * @code{.c}
 * static int my_driver_init(const struct device *dev) {
 *     const struct my_driver_config *cfg = dev->config;
 *     struct my_driver_data *data = dev->data;
 *
 *     INPUT_COMMON_INIT_POLLING(dev, data, work, my_work_handler,
 *                               timer, my_timer_handler,
 *                               cfg->poll_interval_ms);
 *     // ... driver-specific init
 *     return 0;
 * }
 * @endcode
 */
#define INPUT_COMMON_INIT_POLLING(_dev, _data, _work, _work_handler, _timer,  \
				  _timer_handler, _period_ms)                 \
	do {                                                                   \
		(_data)->dev = (_dev);                                         \
		INPUT_WORK_INIT(&(_data)->_work, _work_handler);              \
		INPUT_POLL_TIMER_INIT(&(_data)->_timer, _timer_handler,       \
				      _period_ms);                             \
	} while (0)

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_INPUT_DRIVER_H_ */
