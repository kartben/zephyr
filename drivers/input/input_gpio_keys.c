/*
 * Copyright (c) 2022 Google LLC
 * Copyright (c) 2024 Google LLC  (Ada bridge additions)
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO Keys input driver – C file.
 *
 * This file provides:
 *   1. All struct definitions (used by the DT macros and the test suite).
 *   2. Devicetree-macro-generated per-instance device registration.
 *
 * When the Ada implementation is active (GPIO_KEYS_ADA_IMPL is defined,
 * which CMake sets when a suitable GNAT compiler is detected):
 *   3a. Forward declarations for the Ada-exported entry-points.
 *   3b. Thin C accessor helpers called from Ada.
 *   3c. Thin C logging wrappers (Ada cannot call LOG_* macros).
 *
 * When the Ada implementation is NOT available:
 *   3d. The original C implementations of all entry-points (fallback).
 */

#define DT_DRV_COMPAT gpio_keys

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(gpio_keys, CONFIG_INPUT_LOG_LEVEL);

/* =========================================================================
 * 1. Struct definitions
 * =========================================================================
 */

struct gpio_keys_callback {
	struct gpio_callback gpio_cb;
	int8_t pin_state;
};

struct gpio_keys_pin_config {
	/** GPIO specification from devicetree */
	struct gpio_dt_spec spec;
	/** Zephyr code from devicetree */
	uint32_t zephyr_code;
};

struct gpio_keys_pin_data {
	const struct device *dev;
	struct gpio_keys_callback cb_data;
	struct k_work_delayable work;
};

struct gpio_keys_config {
	/** Debounce interval in milliseconds from devicetree */
	uint32_t debounce_interval_ms;
	const int num_keys;
	const struct gpio_keys_pin_config *pin_cfg;
	struct gpio_keys_pin_data *pin_data;
	k_work_handler_t handler;
	bool polling_mode;
	bool no_disconnect;
};

struct gpio_keys_data {
#ifdef CONFIG_PM_DEVICE
	atomic_t suspended;
#endif
};

/* =========================================================================
 * 2a. Ada bridge path (GPIO_KEYS_ADA_IMPL defined by CMake when GNAT found)
 * =========================================================================
 */
#ifdef GPIO_KEYS_ADA_IMPL

/*
 * Forward declarations for Ada-exported entry-points.
 * These symbols are defined in input_gpio_keys_ada.adb and exported with
 * C linkage via "pragma Export(C, ...)".
 */
extern int  gpio_keys_init(const struct device *dev);
extern void gpio_keys_change_deferred(struct k_work *work);
extern void gpio_keys_poll_pins(struct k_work *work);
extern void gpio_keys_interrupt(const struct device *port,
struct gpio_callback *cb,
uint32_t pins);
extern int  gpio_keys_pm_action(const struct device *dev,
enum pm_device_action action);

/* -------------------------------------------------------------------------
 * Accessor helpers  –  thin wrappers exposing struct fields to Ada.
 * Ada passes void * (System.Address) for all struct pointers so no C type
 * information leaks into the Ada source.
 * -------------------------------------------------------------------------
 */

const void *gpio_keys_acc_get_config(const struct device *dev)
{
return dev->config;
}

void *gpio_keys_acc_get_data(const struct device *dev)
{
return dev->data;
}

const char *gpio_keys_acc_dev_name(const struct device *dev)
{
return dev->name;
}

int gpio_keys_acc_num_keys(const struct gpio_keys_config *cfg)
{
return cfg->num_keys;
}

const struct gpio_keys_pin_config *gpio_keys_acc_pin_cfg(
const struct gpio_keys_config *cfg, int i)
{
return &cfg->pin_cfg[i];
}

struct gpio_keys_pin_data *gpio_keys_acc_pin_data(
const struct gpio_keys_config *cfg, int i)
{
return &cfg->pin_data[i];
}

uint32_t gpio_keys_acc_debounce_ms(const struct gpio_keys_config *cfg)
{
return cfg->debounce_interval_ms;
}

int gpio_keys_acc_polling_mode(const struct gpio_keys_config *cfg)
{
return (int)cfg->polling_mode;
}

int gpio_keys_acc_no_disconnect(const struct gpio_keys_config *cfg)
{
return (int)cfg->no_disconnect;
}

const struct gpio_dt_spec *gpio_keys_acc_pin_cfg_spec(
const struct gpio_keys_pin_config *pin_cfg)
{
return &pin_cfg->spec;
}

uint32_t gpio_keys_acc_pin_cfg_code(const struct gpio_keys_pin_config *pin_cfg)
{
return pin_cfg->zephyr_code;
}

const char *gpio_keys_acc_spec_port_name(const struct gpio_dt_spec *spec)
{
return spec->port->name;
}

const struct device *gpio_keys_acc_pin_data_dev(
const struct gpio_keys_pin_data *pin_data)
{
return pin_data->dev;
}

void gpio_keys_acc_pin_data_set_dev(struct gpio_keys_pin_data *pin_data,
    const struct device *dev)
{
pin_data->dev = dev;
}

int gpio_keys_acc_pin_state(const struct gpio_keys_pin_data *pin_data)
{
return (int)pin_data->cb_data.pin_state;
}

void gpio_keys_acc_set_pin_state(struct gpio_keys_pin_data *pin_data,
 int state)
{
pin_data->cb_data.pin_state = (int8_t)state;
}

struct gpio_keys_callback *gpio_keys_acc_pin_data_cb(
struct gpio_keys_pin_data *pin_data)
{
return &pin_data->cb_data;
}

struct k_work_delayable *gpio_keys_acc_pin_data_work(
struct gpio_keys_pin_data *pin_data)
{
return &pin_data->work;
}

struct gpio_keys_pin_data *gpio_keys_acc_work_to_pin_data(struct k_work *work)
{
struct k_work_delayable *dwork = k_work_delayable_from_work(work);

return CONTAINER_OF(dwork, struct gpio_keys_pin_data, work);
}

struct gpio_keys_callback *gpio_keys_acc_keys_cb_from_cb(
struct gpio_callback *cbdata)
{
return CONTAINER_OF(cbdata, struct gpio_keys_callback, gpio_cb);
}

struct gpio_keys_pin_data *gpio_keys_acc_cb_to_pin_data(
struct gpio_keys_callback *keys_cb)
{
return CONTAINER_OF(keys_cb, struct gpio_keys_pin_data, cb_data);
}

int gpio_keys_acc_pin_data_index(const struct gpio_keys_config *cfg,
 const struct gpio_keys_pin_data *pin_data)
{
return (int)(pin_data - cfg->pin_data);
}

int gpio_keys_acc_is_suspended(const struct gpio_keys_data *data)
{
#ifdef CONFIG_PM_DEVICE
return (int)atomic_get(&data->suspended);
#else
ARG_UNUSED(data);
return 0;
#endif
}

void gpio_keys_acc_set_suspended(struct gpio_keys_data *data, int val)
{
#ifdef CONFIG_PM_DEVICE
atomic_set(&data->suspended, val);
#else
ARG_UNUSED(data);
ARG_UNUSED(val);
#endif
}

void gpio_keys_acc_work_init(struct gpio_keys_pin_data *pin_data,
     int is_polling)
{
k_work_handler_t handler = is_polling ? gpio_keys_poll_pins
      : gpio_keys_change_deferred;

k_work_init_delayable(&pin_data->work, handler);
}

void gpio_keys_acc_work_reschedule(struct k_work_delayable *dwork,
   uint32_t ms)
{
k_work_reschedule(dwork, K_MSEC(ms));
}

int gpio_keys_acc_configure_interrupt(const struct gpio_dt_spec *spec,
      struct gpio_keys_callback *cb,
      struct gpio_keys_pin_data *pin_data)
{
int ret;

ARG_UNUSED(pin_data);

gpio_init_callback(&cb->gpio_cb, gpio_keys_interrupt, BIT(spec->pin));

ret = gpio_add_callback(spec->port, &cb->gpio_cb);
if (ret < 0) {
LOG_ERR("Could not set gpio callback");
return ret;
}

cb->pin_state = gpio_pin_get_dt(spec);

LOG_DBG("port=%s, pin=%d", spec->port->name, spec->pin);

ret = gpio_pin_interrupt_configure_dt(spec, GPIO_INT_EDGE_BOTH);
if (ret < 0) {
LOG_ERR("interrupt configuration failed: %d", ret);
return ret;
}

return 0;
}

int gpio_keys_acc_gpio_configure_input(const struct gpio_dt_spec *spec)
{
return gpio_pin_configure_dt(spec, GPIO_INPUT);
}

int gpio_keys_acc_gpio_configure_disconnected(const struct gpio_dt_spec *spec)
{
return gpio_pin_configure_dt(spec, GPIO_DISCONNECTED);
}

int gpio_keys_acc_gpio_interrupt_disable(const struct gpio_dt_spec *spec)
{
return gpio_pin_interrupt_configure_dt(spec, GPIO_INT_DISABLE);
}

int gpio_keys_acc_gpio_interrupt_enable_both(const struct gpio_dt_spec *spec)
{
return gpio_pin_interrupt_configure_dt(spec, GPIO_INT_EDGE_BOTH);
}

void gpio_keys_acc_report_key(const struct device *dev, uint32_t code,
      int pressed)
{
input_report_key(dev, (uint16_t)code, pressed, true, K_FOREVER);
}

/* -------------------------------------------------------------------------
 * Logging wrappers (LOG_* are C macros; Ada calls these thin shims)
 * -------------------------------------------------------------------------
 */

void gpio_keys_log_err_key_get_failed(int key_index, int ret)
{
LOG_ERR("key_index %d get failed: %d", key_index, ret);
}

void gpio_keys_log_dbg_poll(const char *name, int pin_state,
    int new_pressed, int key_index)
{
LOG_DBG("%s: pin_state=%d, new_pressed=%d, key_index=%d",
name, pin_state, new_pressed, key_index);
}

void gpio_keys_log_dbg_report(const char *name, int new_pressed,
      uint32_t code)
{
LOG_DBG("Report event %s %d, code=%d", name, new_pressed, code);
}

void gpio_keys_log_err_not_ready(const char *port_name)
{
LOG_ERR("%s is not ready", port_name);
}

void gpio_keys_log_err_pin_config_failed(int i, int ret)
{
LOG_ERR("Pin %d configuration failed: %d", i, ret);
}

void gpio_keys_log_err_interrupt_config_failed(int i, int ret)
{
LOG_ERR("Pin %d interrupt configuration failed: %d", i, ret);
}

void gpio_keys_log_err_pm_failed(int ret)
{
ARG_UNUSED(ret);
LOG_ERR("Failed to enable runtime power management");
}

void gpio_keys_log_err_interrupt_configure_failed(int ret)
{
LOG_ERR("interrupt configuration failed: %d", ret);
}

#else /* !GPIO_KEYS_ADA_IMPL – original C fallback implementation */

/* =========================================================================
 * 2b. Original C implementation (compiled when Ada compiler is unavailable)
 * =========================================================================
 */

/**
 * Handle debounced gpio pin state.
 */
static void gpio_keys_poll_pin(const struct device *dev, int key_index)
{
const struct gpio_keys_config *cfg = dev->config;
const struct gpio_keys_pin_config *pin_cfg = &cfg->pin_cfg[key_index];
struct gpio_keys_pin_data *pin_data = &cfg->pin_data[key_index];
int new_pressed;
int ret;

ret = gpio_pin_get_dt(&pin_cfg->spec);
if (ret < 0) {
LOG_ERR("key_index %d get failed: %d", key_index, ret);
return;
}

new_pressed = ret;
LOG_DBG("%s: pin_state=%d, new_pressed=%d, key_index=%d", dev->name,
pin_data->cb_data.pin_state, new_pressed, key_index);

/* If gpio changed, report the event */
if (new_pressed != pin_data->cb_data.pin_state) {
pin_data->cb_data.pin_state = new_pressed;
LOG_DBG("Report event %s %d, code=%d", dev->name, new_pressed,
pin_cfg->zephyr_code);
input_report_key(dev, pin_cfg->zephyr_code, new_pressed, true,
 K_FOREVER);
}
}

static __maybe_unused void gpio_keys_poll_pins(struct k_work *work)
{
struct k_work_delayable *dwork = k_work_delayable_from_work(work);
struct gpio_keys_pin_data *pin_data =
CONTAINER_OF(dwork, struct gpio_keys_pin_data, work);
const struct device *dev = pin_data->dev;
const struct gpio_keys_config *cfg = dev->config;

#ifdef CONFIG_PM_DEVICE
struct gpio_keys_data *data = dev->data;

if (atomic_get(&data->suspended) == 1) {
return;
}
#endif

for (int i = 0; i < cfg->num_keys; i++) {
gpio_keys_poll_pin(dev, i);
}

k_work_reschedule(dwork, K_MSEC(cfg->debounce_interval_ms));
}

static __maybe_unused void gpio_keys_change_deferred(struct k_work *work)
{
const struct k_work_delayable *dwork = k_work_delayable_from_work(work);
const struct gpio_keys_pin_data *pin_data =
CONTAINER_OF(dwork, struct gpio_keys_pin_data, work);
const struct device *dev = pin_data->dev;
const struct gpio_keys_config *cfg = dev->config;
const int key_index =
pin_data - (struct gpio_keys_pin_data *)cfg->pin_data;

#ifdef CONFIG_PM_DEVICE
struct gpio_keys_data *data = dev->data;

if (atomic_get(&data->suspended) == 1) {
return;
}
#endif

gpio_keys_poll_pin(dev, key_index);
}

static void gpio_keys_interrupt(const struct device *dev,
struct gpio_callback *cbdata, uint32_t pins)
{
struct gpio_keys_callback *keys_cb =
CONTAINER_OF(cbdata, struct gpio_keys_callback, gpio_cb);
struct gpio_keys_pin_data *pin_data =
CONTAINER_OF(keys_cb, struct gpio_keys_pin_data, cb_data);
const struct gpio_keys_config *cfg = pin_data->dev->config;

ARG_UNUSED(dev);
ARG_UNUSED(pins);

k_work_reschedule(&pin_data->work,
  K_MSEC(cfg->debounce_interval_ms));
}

static int gpio_keys_interrupt_configure(const struct gpio_dt_spec *gpio_spec,
 struct gpio_keys_callback *cb,
 uint32_t zephyr_code)
{
int ret;

ARG_UNUSED(zephyr_code);

gpio_init_callback(&cb->gpio_cb, gpio_keys_interrupt,
   BIT(gpio_spec->pin));

ret = gpio_add_callback(gpio_spec->port, &cb->gpio_cb);
if (ret < 0) {
LOG_ERR("Could not set gpio callback");
return ret;
}

cb->pin_state = gpio_pin_get_dt(gpio_spec);

LOG_DBG("port=%s, pin=%d", gpio_spec->port->name, gpio_spec->pin);

ret = gpio_pin_interrupt_configure_dt(gpio_spec, GPIO_INT_EDGE_BOTH);
if (ret < 0) {
LOG_ERR("interrupt configuration failed: %d", ret);
return ret;
}

return 0;
}

static int gpio_keys_init(const struct device *dev)
{
const struct gpio_keys_config *cfg = dev->config;
struct gpio_keys_pin_data *pin_data = cfg->pin_data;
int ret;

for (int i = 0; i < cfg->num_keys; i++) {
const struct gpio_dt_spec *gpio = &cfg->pin_cfg[i].spec;

if (!gpio_is_ready_dt(gpio)) {
LOG_ERR("%s is not ready", gpio->port->name);
return -ENODEV;
}

ret = gpio_pin_configure_dt(gpio, GPIO_INPUT);
if (ret != 0) {
LOG_ERR("Pin %d configuration failed: %d", i, ret);
return ret;
}

pin_data[i].dev = dev;
k_work_init_delayable(&pin_data[i].work, cfg->handler);

if (cfg->polling_mode) {
continue;
}

ret = gpio_keys_interrupt_configure(&cfg->pin_cfg[i].spec,
    &pin_data[i].cb_data,
    cfg->pin_cfg[i].zephyr_code);
if (ret != 0) {
LOG_ERR("Pin %d interrupt configuration failed: %d",
i, ret);
return ret;
}
}

if (cfg->polling_mode) {
k_work_reschedule(&pin_data[0].work,
  K_MSEC(cfg->debounce_interval_ms));
}

ret = pm_device_runtime_enable(dev);
if (ret < 0) {
LOG_ERR("Failed to enable runtime power management");
return ret;
}

return 0;
}

#ifdef CONFIG_PM_DEVICE
static int gpio_keys_pm_action(const struct device *dev,
       enum pm_device_action action)
{
const struct gpio_keys_config *cfg = dev->config;
struct gpio_keys_data *data = dev->data;
struct gpio_keys_pin_data *pin_data = cfg->pin_data;
int ret;

switch (action) {
case PM_DEVICE_ACTION_SUSPEND:
atomic_set(&data->suspended, 1);

for (int i = 0; i < cfg->num_keys; i++) {
const struct gpio_dt_spec *gpio = &cfg->pin_cfg[i].spec;

if (!cfg->polling_mode) {
ret = gpio_pin_interrupt_configure_dt(
gpio, GPIO_INT_DISABLE);
if (ret < 0) {
LOG_ERR("interrupt configuration failed: %d",
ret);
return ret;
}
}

if (!cfg->no_disconnect) {
ret = gpio_pin_configure_dt(gpio,
    GPIO_DISCONNECTED);
if (ret != 0) {
LOG_ERR("Pin %d configuration failed: %d",
i, ret);
return ret;
}
}
}

return 0;
case PM_DEVICE_ACTION_RESUME:
atomic_set(&data->suspended, 0);

for (int i = 0; i < cfg->num_keys; i++) {
const struct gpio_dt_spec *gpio = &cfg->pin_cfg[i].spec;

if (!cfg->no_disconnect) {
ret = gpio_pin_configure_dt(gpio, GPIO_INPUT);
if (ret != 0) {
LOG_ERR("Pin %d configuration failed: %d",
i, ret);
return ret;
}
}

if (cfg->polling_mode) {
k_work_reschedule(
&pin_data[0].work,
K_MSEC(cfg->debounce_interval_ms));
} else {
pin_data[i].cb_data.pin_state =
gpio_pin_get_dt(gpio);
ret = gpio_pin_interrupt_configure_dt(
gpio, GPIO_INT_EDGE_BOTH);
if (ret < 0) {
LOG_ERR("interrupt configuration failed: %d",
ret);
return ret;
}
}
}

return 0;
default:
return -ENOTSUP;
}
}
#endif /* CONFIG_PM_DEVICE */

#endif /* GPIO_KEYS_ADA_IMPL */

/* =========================================================================
 * 3. Devicetree-macro-generated per-instance device registration
 *    (always compiled regardless of Ada/C implementation choice)
 * =========================================================================
 */

#define GPIO_KEYS_CFG_CHECK(node_id)                                           \
BUILD_ASSERT(DT_NODE_HAS_PROP(node_id, zephyr_code),                   \
     "zephyr-code must be specified to use the input-gpio-keys driver");

#define GPIO_KEYS_CFG_DEF(node_id)                                             \
{                                                                       \
.spec = GPIO_DT_SPEC_GET(node_id, gpios),                       \
.zephyr_code = DT_PROP(node_id, zephyr_code),                  \
}

#define GPIO_KEYS_INIT(i)                                                      \
DT_INST_FOREACH_CHILD_STATUS_OKAY(i, GPIO_KEYS_CFG_CHECK);             \
       \
static const struct gpio_keys_pin_config gpio_keys_pin_config_##i[] = {\
DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(                         \
i, GPIO_KEYS_CFG_DEF, (,))};                           \
       \
static struct gpio_keys_pin_data                                        \
gpio_keys_pin_data_##i[ARRAY_SIZE(gpio_keys_pin_config_##i)];  \
       \
static const struct gpio_keys_config gpio_keys_config_##i = {          \
.debounce_interval_ms = DT_INST_PROP(i, debounce_interval_ms), \
.num_keys = ARRAY_SIZE(gpio_keys_pin_config_##i),              \
.pin_cfg = gpio_keys_pin_config_##i,                           \
.pin_data = gpio_keys_pin_data_##i,                            \
.handler = COND_CODE_1(DT_INST_PROP(i, polling_mode),         \
       (gpio_keys_poll_pins),                  \
       (gpio_keys_change_deferred)),            \
.polling_mode = DT_INST_PROP(i, polling_mode),                 \
.no_disconnect = DT_INST_PROP(i, no_disconnect),               \
};                                                                     \
       \
static struct gpio_keys_data gpio_keys_data_##i;                       \
       \
PM_DEVICE_DT_INST_DEFINE(i, gpio_keys_pm_action);                      \
       \
DEVICE_DT_INST_DEFINE(i, &gpio_keys_init, PM_DEVICE_DT_INST_GET(i),   \
      &gpio_keys_data_##i, &gpio_keys_config_##i,     \
      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(GPIO_KEYS_INIT)
