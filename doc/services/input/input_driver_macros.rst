.. _input_driver_macros:

Input Driver Helper Macros
###########################

Overview
********

The Input subsystem provides a set of helper macros to reduce boilerplate code
in input drivers while maintaining readability. These macros encapsulate common
patterns found across multiple input drivers, making it easier to write new
drivers and maintain existing ones.

Common Patterns Abstracted
***************************

The helper macros address several repetitive patterns:

1. **Device readiness checks** - I2C, SPI, and GPIO device validation
2. **GPIO interrupt setup** - Pin configuration, callback initialization, interrupt enable
3. **Work queue initialization** - Both standard and delayable work queues
4. **Polling timer setup** - Timer initialization and periodic start
5. **Combined initialization** - Common init sequences for interrupt and polling modes

Benefits
********

Using these macros provides several advantages:

* **Reduced code duplication** - Common patterns written once
* **Improved readability** - Intent is clearer than low-level API calls
* **Consistent error handling** - Standard error messages and return codes
* **Easier maintenance** - Changes to patterns only need to be made in one place
* **Lower barrier to entry** - Simpler for new driver authors

Available Macros
****************

Device Readiness Checks
=======================

.. doxygendefine:: INPUT_I2C_CHECK_READY
.. doxygendefine:: INPUT_SPI_CHECK_READY
.. doxygendefine:: INPUT_GPIO_CHECK_READY

GPIO Configuration
==================

.. doxygendefine:: INPUT_GPIO_PIN_CONFIGURE
.. doxygendefine:: INPUT_GPIO_INTERRUPT_INIT

Work Queue Helpers
==================

.. doxygendefine:: INPUT_WORK_INIT
.. doxygendefine:: INPUT_WORK_DELAYABLE_INIT
.. doxygendefine:: INPUT_WORK_SUBMIT
.. doxygendefine:: INPUT_WORK_RESCHEDULE

Timer Helpers
=============

.. doxygendefine:: INPUT_POLL_TIMER_INIT

Combined Initialization
=======================

.. doxygendefine:: INPUT_COMMON_INIT_INTERRUPT
.. doxygendefine:: INPUT_COMMON_INIT_POLLING

Usage Examples
**************

Simple I2C Input Device
=======================

Here's a minimal example of an I2C-based input device using interrupts:

.. code-block:: c

   #include <zephyr/input/input.h>
   #include <zephyr/input/input_driver.h>

   struct my_sensor_config {
       struct i2c_dt_spec i2c;
       struct gpio_dt_spec int_gpio;
   };

   struct my_sensor_data {
       const struct device *dev;
       struct k_work work;
       struct gpio_callback int_gpio_cb;
   };

   static void my_sensor_work_handler(struct k_work *work)
   {
       struct my_sensor_data *data = CONTAINER_OF(work,
                                                   struct my_sensor_data, work);
       const struct device *dev = data->dev;
       /* Read sensor and report input events */
   }

   static void my_sensor_gpio_handler(const struct device *port,
                                      struct gpio_callback *cb,
                                      uint32_t pins)
   {
       struct my_sensor_data *data = CONTAINER_OF(cb,
                                                   struct my_sensor_data,
                                                   int_gpio_cb);
       INPUT_WORK_SUBMIT(&data->work);
   }

   static int my_sensor_init(const struct device *dev)
   {
       const struct my_sensor_config *cfg = dev->config;
       struct my_sensor_data *data = dev->data;

       /* Check I2C bus readiness */
       INPUT_I2C_CHECK_READY(&cfg->i2c);

       /* Initialize work queue and GPIO interrupt */
       INPUT_COMMON_INIT_INTERRUPT(dev, data, work, my_sensor_work_handler,
                                    int_gpio_cb, my_sensor_gpio_handler,
                                    &cfg->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);

       return 0;
   }

Polling Mode Input Device
==========================

Here's an example using polling mode:

.. code-block:: c

   struct my_polling_sensor_config {
       struct i2c_dt_spec i2c;
       uint32_t poll_interval_ms;
   };

   struct my_polling_sensor_data {
       const struct device *dev;
       struct k_work work;
       struct k_timer timer;
   };

   static void my_polling_work_handler(struct k_work *work)
   {
       /* Read sensor and report input events */
   }

   static void my_polling_timer_handler(struct k_timer *timer)
   {
       struct my_polling_sensor_data *data = CONTAINER_OF(timer,
                                                           struct my_polling_sensor_data,
                                                           timer);
       INPUT_WORK_SUBMIT(&data->work);
   }

   static int my_polling_sensor_init(const struct device *dev)
   {
       const struct my_polling_sensor_config *cfg = dev->config;
       struct my_polling_sensor_data *data = dev->data;

       /* Check I2C bus readiness */
       INPUT_I2C_CHECK_READY(&cfg->i2c);

       /* Initialize work queue and polling timer */
       INPUT_COMMON_INIT_POLLING(dev, data, work, my_polling_work_handler,
                                 timer, my_polling_timer_handler,
                                 cfg->poll_interval_ms);

       return 0;
   }

SPI Device with Reset GPIO
===========================

Example showing SPI device with reset GPIO configuration:

.. code-block:: c

   struct my_spi_sensor_config {
       struct spi_dt_spec spi;
       struct gpio_dt_spec reset_gpio;
       struct gpio_dt_spec motion_gpio;
   };

   struct my_spi_sensor_data {
       const struct device *dev;
       struct k_work work;
       struct gpio_callback motion_cb;
   };

   static void my_work_handler(struct k_work *work)
   {
       /* Process sensor data */
   }

   static void my_motion_handler(const struct device *port,
                                 struct gpio_callback *cb,
                                 uint32_t pins)
   {
       struct my_spi_sensor_data *data = CONTAINER_OF(cb,
                                                       struct my_spi_sensor_data,
                                                       motion_cb);
       INPUT_WORK_SUBMIT(&data->work);
   }

   static int my_spi_sensor_init(const struct device *dev)
   {
       const struct my_spi_sensor_config *cfg = dev->config;
       struct my_spi_sensor_data *data = dev->data;

       /* Check SPI bus readiness */
       INPUT_SPI_CHECK_READY(&cfg->spi);

       /* Configure reset GPIO if present */
       if (cfg->reset_gpio.port != NULL) {
           INPUT_GPIO_CHECK_READY(&cfg->reset_gpio);
           INPUT_GPIO_PIN_CONFIGURE(&cfg->reset_gpio, GPIO_OUTPUT_ACTIVE);
           k_sleep(K_MSEC(10));
           gpio_pin_set_dt(&cfg->reset_gpio, 0);
       }

       /* Initialize work queue */
       data->dev = dev;
       INPUT_WORK_INIT(&data->work, my_work_handler);

       /* Setup motion detection interrupt */
       INPUT_GPIO_INTERRUPT_INIT(&cfg->motion_gpio, &data->motion_cb,
                                 my_motion_handler, GPIO_INT_EDGE_TO_ACTIVE);

       return 0;
   }

Dual Mode (Interrupt/Polling) Support
======================================

Example of a driver supporting both interrupt and polling modes:

.. code-block:: c

   struct my_dual_mode_data {
       const struct device *dev;
       struct k_work work;
       struct gpio_callback int_gpio_cb;
       struct k_timer timer;
   };

   static void my_work_handler(struct k_work *work)
   {
       /* Process sensor data */
   }

   static void my_gpio_handler(const struct device *port,
                               struct gpio_callback *cb,
                               uint32_t pins)
   {
       struct my_dual_mode_data *data = CONTAINER_OF(cb,
                                                      struct my_dual_mode_data,
                                                      int_gpio_cb);
       INPUT_WORK_SUBMIT(&data->work);
   }

   static void my_timer_handler(struct k_timer *timer)
   {
       struct my_dual_mode_data *data = CONTAINER_OF(timer,
                                                      struct my_dual_mode_data,
                                                      timer);
       INPUT_WORK_SUBMIT(&data->work);
   }

   static int my_dual_mode_init(const struct device *dev)
   {
       const struct my_dual_mode_config *cfg = dev->config;
       struct my_dual_mode_data *data = dev->data;

       INPUT_I2C_CHECK_READY(&cfg->i2c);

       data->dev = dev;
       INPUT_WORK_INIT(&data->work, my_work_handler);

       if (cfg->int_gpio.port != NULL) {
           /* Interrupt mode */
           INPUT_GPIO_INTERRUPT_INIT(&cfg->int_gpio, &data->int_gpio_cb,
                                     my_gpio_handler, GPIO_INT_EDGE_TO_ACTIVE);
       } else {
           /* Polling mode */
           INPUT_POLL_TIMER_INIT(&data->timer, my_timer_handler,
                                 cfg->poll_interval_ms);
       }

       return 0;
   }

Best Practices
**************

When to Use These Macros
=========================

* Use these macros for **new input drivers** to reduce boilerplate
* Consider using them when **refactoring existing drivers** for consistency
* They're most beneficial for **simple to moderately complex** drivers
* For very complex or unusual patterns, direct API usage may be clearer

When Not to Use These Macros
=============================

* When the driver needs **non-standard error handling**
* When **additional logic** is needed between init steps
* When the macro would **obscure the intent** more than help
* For **one-off patterns** that aren't common across drivers

Code Style Considerations
==========================

* Macros follow Zephyr coding style (8-space tabs, 100-char lines)
* Use the macros consistently within a driver
* Add driver-specific comments where macros are used
* Don't nest macro calls unless the result is still readable

**Note on Flow Control in Macros**: Some macros intentionally use flow control
(``return`` statements) to reduce boilerplate error handling. While this
generates checkpatch warnings (``MACRO_WITH_FLOW_CONTROL``), it's an accepted
pattern for these helper macros as the benefits in reduced code and improved
readability outweigh the warning. The macros are carefully designed to be used
only in appropriate contexts (driver init functions).

Maintenance
***********

If you need to modify the behavior of these macros:

1. Consider backward compatibility
2. Update documentation examples
3. Test with existing drivers using the macros
4. Consider adding a new macro rather than changing existing ones

API Reference
*************

See :zephyr_file:`include/zephyr/input/input_driver.h` for the complete API.
