.. _sensor_driver_helpers:

Sensor Driver Helper Macros
############################

Overview
********

The sensor driver helper macros provide a set of utilities to reduce boilerplate
code in sensor drivers while maintaining code readability and consistency across
the Zephyr sensor subsystem. These macros abstract common patterns found in
sensor drivers, making driver development faster and less error-prone.

Benefits
********

Using these helper macros provides several advantages:

* **Reduced Code Duplication**: Eliminates repetitive patterns across drivers
* **Improved Consistency**: Ensures uniform implementation of common operations
* **Enhanced Readability**: Makes driver intent clearer by reducing boilerplate
* **Easier Maintenance**: Updates to common patterns can be centralized
* **Fewer Bugs**: Well-tested macro implementations reduce driver-specific errors

Common Patterns Addressed
**************************

The helper macros address these common boilerplate patterns:

1. **Register Access**: I2C and SPI register read/write wrappers
2. **Bus Validation**: Checking if I2C/SPI bus is ready during initialization
3. **Power Management**: Standard PM initialization and no-op handlers
4. **Channel Dispatching**: Switch-based routing in sample_fetch and channel_get
5. **Trigger Handling**: GPIO interrupt setup and work queue management
6. **Device Instantiation**: Device tree-based device registration

Usage Examples
**************

Basic I2C Temperature Sensor
============================

Here's a minimal example for a simple I2C temperature sensor:

.. code-block:: c

   #define DT_DRV_COMPAT example_temp_sensor

   #include <zephyr/drivers/sensor/sensor_driver_helpers.h>

   /* Define data and config structures */
   struct example_data {
       struct sensor_value temp;
   };

   struct example_config {
       struct i2c_dt_spec i2c;
   };

   /* Generate register access functions */
   SENSOR_I2C_REG_ACCESS(example, example_config, i2c)

   /* Implement sensor-specific logic */
   static int example_fetch_temp(const struct device *dev)
   {
       struct example_data *data = dev->data;
       uint8_t raw[2];
       int ret;

       ret = example_reg_read(dev, TEMP_REG, raw, 2);
       if (ret < 0) {
           return ret;
       }

       /* Convert raw value to sensor_value */
       data->temp.val1 = raw[0];
       data->temp.val2 = raw[1] * 10000;

       return 0;
   }

   /* Use helper macros for channel handling */
   SENSOR_SAMPLE_FETCH_SIMPLE(example,
       SENSOR_CHAN_ALL, example_fetch_temp,
       SENSOR_CHAN_AMBIENT_TEMP, example_fetch_temp)

   SENSOR_CHANNEL_GET_SIMPLE(example, example_data,
       SENSOR_CHAN_AMBIENT_TEMP, temp)

   /* Define driver API */
   static DEVICE_API(sensor, example_driver_api) = {
       .sample_fetch = example_sample_fetch,
       .channel_get = example_channel_get,
   };

   /* Generate PM handler */
   SENSOR_PM_ACTION_NOOP(example)

   /* Initialization function */
   static int example_init(const struct device *dev)
   {
       const struct example_config *cfg = dev->config;

       SENSOR_DEVICE_I2C_BUS_READY_CHECK(dev, cfg, i2c);
       SENSOR_DEVICE_PM_INIT(dev);

       /* Sensor-specific initialization */
       return example_reg_write(dev, CONFIG_REG, &(uint8_t){0x01}, 1);
   }

   /* Device instantiation */
   #define EXAMPLE_INST(inst) \
       SENSOR_DEVICE_DT_INST_I2C_DEFINE(example, inst, \
           example_data, example_config, \
           example_init, example_pm_action, example_driver_api)

   DT_INST_FOREACH_STATUS_OKAY(EXAMPLE_INST)

Sensor with GPIO Trigger Support
=================================

For sensors that support interrupt-driven operation:

.. code-block:: c

   #define DT_DRV_COMPAT example_accel_sensor

   #include <zephyr/drivers/sensor/sensor_driver_helpers.h>

   struct example_data {
       const struct device *dev;
       struct sensor_value accel[3];

       #if defined(CONFIG_EXAMPLE_TRIGGER)
       struct k_work work;
       struct k_work_q workq;
       struct gpio_callback gpio_cb;
       const struct sensor_trigger *trigger;
       sensor_trigger_handler_t trigger_handler;
       K_KERNEL_STACK_MEMBER(stack, CONFIG_EXAMPLE_TRIGGER_THREAD_STACK_SIZE);
       #endif
   };

   struct example_config {
       struct i2c_dt_spec i2c;
       #if defined(CONFIG_EXAMPLE_TRIGGER)
       struct gpio_dt_spec int_gpio;
       #endif
   };

   SENSOR_I2C_REG_ACCESS(example, example_config, i2c)

   #if defined(CONFIG_EXAMPLE_TRIGGER)
   /* Generate trigger helper functions */
   SENSOR_TRIGGER_WORK_HANDLER(example, example_data)
   SENSOR_TRIGGER_GPIO_CALLBACK(example, example_data)

   static int example_trigger_set(const struct device *dev,
                                   const struct sensor_trigger *trig,
                                   sensor_trigger_handler_t handler)
   {
       const struct example_config *cfg = dev->config;
       struct example_data *data = dev->data;
       gpio_flags_t flags;
       int ret;

       if (cfg->int_gpio.port == NULL) {
           return -ENOTSUP;
       }

       flags = handler ? GPIO_INT_EDGE_TO_ACTIVE : GPIO_INT_DISABLE;
       ret = gpio_pin_interrupt_configure_dt(&cfg->int_gpio, flags);
       if (ret < 0) {
           return ret;
       }

       data->trigger = trig;
       data->trigger_handler = handler;

       return 0;
   }
   #endif /* CONFIG_EXAMPLE_TRIGGER */

   static int example_init(const struct device *dev)
   {
       const struct example_config *cfg = dev->config;

       SENSOR_DEVICE_I2C_BUS_READY_CHECK(dev, cfg, i2c);
       SENSOR_DEVICE_PM_INIT(dev);

       #if defined(CONFIG_EXAMPLE_TRIGGER)
       struct example_data *data = dev->data;
       SENSOR_TRIGGER_GPIO_INIT(data, cfg, int_gpio,
                                example_gpio_callback,
                                example_trigger_work_handler,
                                workq, stack,
                                CONFIG_EXAMPLE_TRIGGER_THREAD_STACK_SIZE,
                                CONFIG_EXAMPLE_TRIGGER_THREAD_PRIO,
                                "example_trigger");
       #endif

       return 0;
   }

   static DEVICE_API(sensor, example_driver_api) = {
       .sample_fetch = example_sample_fetch,
       .channel_get = example_channel_get,
       #if defined(CONFIG_EXAMPLE_TRIGGER)
       .trigger_set = example_trigger_set,
       #endif
   };

   SENSOR_PM_ACTION_NOOP(example)

   #define EXAMPLE_INST(inst) \
       SENSOR_DEVICE_DT_INST_I2C_DEFINE_WITH_TRIGGER(example, inst, \
           example_data, example_config, \
           example_init, example_pm_action, example_driver_api, \
           int_gpios)

   DT_INST_FOREACH_STATUS_OKAY(EXAMPLE_INST)

SPI-based Sensor
================

For sensors that use SPI instead of I2C:

.. code-block:: c

   #define DT_DRV_COMPAT example_spi_sensor

   #include <zephyr/drivers/sensor/sensor_driver_helpers.h>

   struct example_config {
       struct spi_dt_spec spi;
   };

   /* Generate SPI register access functions */
   SENSOR_SPI_REG_ACCESS(example, example_config, spi)

   static int example_init(const struct device *dev)
   {
       const struct example_config *cfg = dev->config;

       SENSOR_DEVICE_SPI_BUS_READY_CHECK(dev, cfg, spi);
       SENSOR_DEVICE_PM_INIT(dev);

       /* Rest of initialization */
       return 0;
   }

   SENSOR_PM_ACTION_NOOP(example)

   #define EXAMPLE_INST(inst) \
       SENSOR_DEVICE_DT_INST_SPI_DEFINE(example, inst, \
           example_data, example_config, \
           example_init, example_pm_action, example_driver_api)

   DT_INST_FOREACH_STATUS_OKAY(EXAMPLE_INST)

API Reference
*************

For complete API documentation, see :ref:`sensor_driver_helpers_api`.

Migration Guide
***************

Converting Existing Drivers
============================

To convert an existing sensor driver to use these helper macros:

1. **Include the header**:

   .. code-block:: c

      #include <zephyr/drivers/sensor/sensor_driver_helpers.h>

2. **Replace register access functions**:

   Before:

   .. code-block:: c

      static inline int lm75_reg_read(const struct device *dev,
                                      uint8_t reg, uint8_t *buf,
                                      uint32_t size)
      {
          const struct lm75_config *cfg = dev->config;
          return i2c_burst_read_dt(&cfg->i2c, reg, buf, size);
      }

   After:

   .. code-block:: c

      SENSOR_I2C_REG_ACCESS(lm75, lm75_config, i2c)

3. **Simplify initialization code**:

   Before:

   .. code-block:: c

      if (!device_is_ready(cfg->i2c.bus)) {
          LOG_ERR("I2C bus not ready");
          return -ENODEV;
      }

      #ifdef CONFIG_PM_DEVICE_RUNTIME
      pm_device_init_suspended(dev);
      ret = pm_device_runtime_enable(dev);
      if (ret < 0 && ret != -ENOTSUP) {
          return ret;
      }
      #endif

   After:

   .. code-block:: c

      SENSOR_DEVICE_I2C_BUS_READY_CHECK(dev, cfg, i2c);
      SENSOR_DEVICE_PM_INIT(dev);

4. **Simplify PM handlers** (if applicable):

   Before:

   .. code-block:: c

      #ifdef CONFIG_PM_DEVICE
      static int lm75_pm_action(const struct device *dev,
                                enum pm_device_action action)
      {
          switch (action) {
          case PM_DEVICE_ACTION_TURN_ON:
          case PM_DEVICE_ACTION_RESUME:
          case PM_DEVICE_ACTION_TURN_OFF:
          case PM_DEVICE_ACTION_SUSPEND:
              break;
          default:
              return -ENOTSUP;
          }
          return 0;
      }
      #endif

   After:

   .. code-block:: c

      SENSOR_PM_ACTION_NOOP(lm75)

5. **Simplify device instantiation**:

   Before:

   .. code-block:: c

      #define LM75_INST(inst) \
      static struct lm75_data lm75_data_##inst; \
      static const struct lm75_config lm75_config_##inst = { \
          .i2c = I2C_DT_SPEC_INST_GET(inst), \
      }; \
      PM_DEVICE_DT_INST_DEFINE(inst, lm75_pm_action); \
      SENSOR_DEVICE_DT_INST_DEFINE(inst, lm75_init, \
          PM_DEVICE_DT_INST_GET(inst), \
          &lm75_data_##inst, &lm75_config_##inst, \
          POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, \
          &lm75_driver_api);

   After:

   .. code-block:: c

      #define LM75_INST(inst) \
          SENSOR_DEVICE_DT_INST_I2C_DEFINE(lm75, inst, \
              lm75_data, lm75_config, \
              lm75_init, lm75_pm_action, lm75_driver_api)

Best Practices
**************

When to Use Helper Macros
==========================

* Use helper macros for **standard, repetitive patterns**
* **Do not** use macros when custom logic is needed
* Prefer explicit code over macros when it improves clarity

Code Style
==========

* Helper macros follow Zephyr coding style guidelines
* Generated code uses C89-style comments
* Indentation uses 8-character tabs

Performance Considerations
==========================

* Register access macros use ``static inline`` for zero overhead
* Device instantiation macros generate the same code as manual implementation
* No runtime performance impact

Limitations
***********

* Some macros use variadic arguments, which may not work with very old compilers
* Complex custom initialization logic may not fit the macro patterns
* Macros are designed for common cases; edge cases may require custom code

See Also
********

* :ref:`sensor_api`
* :ref:`device_model_api`
* :ref:`i2c_api`
* :ref:`spi_api`
