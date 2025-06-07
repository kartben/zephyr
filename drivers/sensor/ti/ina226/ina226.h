#ifndef ZEPHYR_DRIVERS_SENSOR_INA226_H_
#define ZEPHYR_DRIVERS_SENSOR_INA226_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#ifdef CONFIG_INA226_TRIGGER
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#endif

struct ina226_data {
    const struct device *dev;
    int16_t current;
    uint16_t bus_voltage;
    uint16_t power;
#ifdef CONFIG_INA226_VSHUNT
    int16_t shunt_voltage;
#endif
    enum sensor_channel chan;
#ifdef CONFIG_INA226_TRIGGER
    struct gpio_callback gpio_cb;
    sensor_trigger_handler_t handler_alert;
    const struct sensor_trigger *trig_alert;
#endif
};

struct ina226_config {
    struct i2c_dt_spec bus;
    uint16_t config;
    uint32_t current_lsb;
    uint16_t cal;
#ifdef CONFIG_INA226_TRIGGER
    bool trig_enabled;
    uint16_t mask;
    struct gpio_dt_spec alert_gpio;
    uint16_t alert_limit;
#endif
};

#ifdef CONFIG_INA226_TRIGGER
int ina226_trigger_mode_init(const struct device *dev);
int ina226_trigger_set(const struct device *dev, const struct sensor_trigger *trig,
                       sensor_trigger_handler_t handler);
#endif

#endif /* ZEPHYR_DRIVERS_SENSOR_INA226_H_ */
