#include "ina226.h"
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(INA226, CONFIG_SENSOR_LOG_LEVEL);

#ifdef CONFIG_INA226_TRIGGER
static void ina226_gpio_callback(const struct device *port,
                                 struct gpio_callback *cb, uint32_t pins)
{
    struct ina226_data *data = CONTAINER_OF(cb, struct ina226_data, gpio_cb);
    ARG_UNUSED(port);
    ARG_UNUSED(pins);
    if (data->handler_alert) {
        data->handler_alert(data->dev, data->trig_alert);
    }
}

int ina226_trigger_set(const struct device *dev, const struct sensor_trigger *trig,
                       sensor_trigger_handler_t handler)
{
    struct ina226_data *data = dev->data;

    ARG_UNUSED(trig);

    data->handler_alert = handler;
    data->trig_alert = trig;

    return 0;
}

int ina226_trigger_mode_init(const struct device *dev)
{
    struct ina226_data *data = dev->data;
    const struct ina226_config *config = dev->config;
    int ret;

    if (!device_is_ready(config->alert_gpio.port)) {
        LOG_ERR("Alert GPIO device not ready");
        return -ENODEV;
    }

    data->dev = dev;

    ret = gpio_pin_configure_dt(&config->alert_gpio, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Could not configure gpio");
        return ret;
    }

    gpio_init_callback(&data->gpio_cb, ina226_gpio_callback,
                       BIT(config->alert_gpio.pin));

    ret = gpio_add_callback(config->alert_gpio.port, &data->gpio_cb);
    if (ret < 0) {
        LOG_ERR("Could not set gpio callback");
        return ret;
    }

    return gpio_pin_interrupt_configure_dt(&config->alert_gpio, GPIO_INT_EDGE_BOTH);
}
#endif /* CONFIG_INA226_TRIGGER */
