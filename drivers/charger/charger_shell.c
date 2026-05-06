/*
 * Copyright 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>

enum charger_shell_prop_type {
	CHARGER_SHELL_PROP_ENUM,
	CHARGER_SHELL_PROP_BOOL,
	CHARGER_SHELL_PROP_UINT,
	CHARGER_SHELL_PROP_CURRENT_NOTIFIER,
};

struct charger_shell_enum {
	const char *name;
	int value;
};

struct charger_shell_prop {
	const char *name;
	charger_prop_t prop;
	enum charger_shell_prop_type type;
	const struct charger_shell_enum *enum_names;
	size_t enum_names_len;
};

#define CHARGER_PROP_ENUM_ENTRY(_name, _prop, _names)                                              \
	{                                                                                          \
		.name = _name,                                                                     \
		.prop = _prop,                                                                     \
		.type = CHARGER_SHELL_PROP_ENUM,                                                   \
		.enum_names = _names,                                                              \
		.enum_names_len = ARRAY_SIZE(_names),                                              \
	}

#define CHARGER_PROP_UINT_ENTRY(_name, _prop)                                                      \
	{                                                                                          \
		.name = _name,                                                                     \
		.prop = _prop,                                                                     \
		.type = CHARGER_SHELL_PROP_UINT,                                                   \
	}

static const struct charger_shell_enum online_names[] = {
	{"offline", CHARGER_ONLINE_OFFLINE},
	{"fixed", CHARGER_ONLINE_FIXED},
	{"programmable", CHARGER_ONLINE_PROGRAMMABLE},
};

static const struct charger_shell_enum status_names[] = {
	{"unknown", CHARGER_STATUS_UNKNOWN},
	{"charging", CHARGER_STATUS_CHARGING},
	{"discharging", CHARGER_STATUS_DISCHARGING},
	{"not-charging", CHARGER_STATUS_NOT_CHARGING},
	{"full", CHARGER_STATUS_FULL},
};

static const struct charger_shell_enum charge_type_names[] = {
	{"unknown", CHARGER_CHARGE_TYPE_UNKNOWN},    {"none", CHARGER_CHARGE_TYPE_NONE},
	{"trickle", CHARGER_CHARGE_TYPE_TRICKLE},    {"fast", CHARGER_CHARGE_TYPE_FAST},
	{"standard", CHARGER_CHARGE_TYPE_STANDARD},  {"adaptive", CHARGER_CHARGE_TYPE_ADAPTIVE},
	{"long-life", CHARGER_CHARGE_TYPE_LONGLIFE}, {"bypass", CHARGER_CHARGE_TYPE_BYPASS},
};

static const struct charger_shell_enum health_names[] = {
	{"unknown", CHARGER_HEALTH_UNKNOWN},
	{"good", CHARGER_HEALTH_GOOD},
	{"overheat", CHARGER_HEALTH_OVERHEAT},
	{"overvoltage", CHARGER_HEALTH_OVERVOLTAGE},
	{"unspecified-failure", CHARGER_HEALTH_UNSPEC_FAILURE},
	{"cold", CHARGER_HEALTH_COLD},
	{"watchdog-timer-expire", CHARGER_HEALTH_WATCHDOG_TIMER_EXPIRE},
	{"safety-timer-expire", CHARGER_HEALTH_SAFETY_TIMER_EXPIRE},
	{"calibration-required", CHARGER_HEALTH_CALIBRATION_REQUIRED},
	{"warm", CHARGER_HEALTH_WARM},
	{"cool", CHARGER_HEALTH_COOL},
	{"hot", CHARGER_HEALTH_HOT},
	{"no-battery", CHARGER_HEALTH_NO_BATTERY},
};

static const struct charger_shell_enum severity_names[] = {
	{"peak", CHARGER_SEVERITY_PEAK},
	{"critical", CHARGER_SEVERITY_CRITICAL},
	{"warning", CHARGER_SEVERITY_WARNING},
};

/* Keep names sorted for shell dynamic command completion (see shell_dynamic_get). */
static const struct charger_shell_prop charger_props[] = {
	CHARGER_PROP_UINT_ENTRY("charge-term-current-ua", CHARGER_PROP_CHARGE_TERM_CURRENT_UA),
	CHARGER_PROP_ENUM_ENTRY("charge-type", CHARGER_PROP_CHARGE_TYPE, charge_type_names),
	CHARGER_PROP_UINT_ENTRY("constant-charge-current-ua",
				CHARGER_PROP_CONSTANT_CHARGE_CURRENT_UA),
	CHARGER_PROP_UINT_ENTRY("constant-charge-voltage-uv",
				CHARGER_PROP_CONSTANT_CHARGE_VOLTAGE_UV),
	{"discharge-current-notification", CHARGER_PROP_DISCHARGE_CURRENT_NOTIFICATION,
	 CHARGER_SHELL_PROP_CURRENT_NOTIFIER},
	CHARGER_PROP_ENUM_ENTRY("health", CHARGER_PROP_HEALTH, health_names),
	{"input-current-notification", CHARGER_PROP_INPUT_CURRENT_NOTIFICATION,
	 CHARGER_SHELL_PROP_CURRENT_NOTIFIER},
	CHARGER_PROP_UINT_ENTRY("input-regulation-current-ua",
				CHARGER_PROP_INPUT_REGULATION_CURRENT_UA),
	CHARGER_PROP_UINT_ENTRY("input-regulation-voltage-uv",
				CHARGER_PROP_INPUT_REGULATION_VOLTAGE_UV),
	CHARGER_PROP_ENUM_ENTRY("online", CHARGER_PROP_ONLINE, online_names),
	{"present", CHARGER_PROP_PRESENT, CHARGER_SHELL_PROP_BOOL},
	CHARGER_PROP_UINT_ENTRY("precharge-current-ua", CHARGER_PROP_PRECHARGE_CURRENT_UA),
	CHARGER_PROP_ENUM_ENTRY("status", CHARGER_PROP_STATUS, status_names),
	CHARGER_PROP_UINT_ENTRY("system-voltage-notification-uv",
				CHARGER_PROP_SYSTEM_VOLTAGE_NOTIFICATION_UV),
};

static const char *enum_to_name(const struct charger_shell_enum *names, size_t len, int value)
{
	for (size_t i = 0; i < len; i++) {
		if (names[i].value == value) {
			return names[i].name;
		}
	}

	return "unknown";
}

static int enum_from_name(const struct charger_shell_enum *names, size_t len, const char *name,
			  int *value)
{
	for (size_t i = 0; i < len; i++) {
		if (strcmp(names[i].name, name) == 0) {
			*value = names[i].value;
			return 0;
		}
	}

	return -EINVAL;
}

static int uint_from_string(const char *str, uint32_t *value)
{
	unsigned long ul;
	int err = 0;

	ul = shell_strtoul(str, 0, &err);
	if (err != 0) {
		return err;
	}
	if (ul > UINT32_MAX) {
		return -EINVAL;
	}

	*value = (uint32_t)ul;
	return 0;
}

static int parse_enum_charger_prop(const struct charger_shell_prop *prop, size_t argc, char **argv,
				   union charger_propval *val)
{
	int enum_value;

	if (argc != 1) {
		return -EINVAL;
	}
	if (enum_from_name(prop->enum_names, prop->enum_names_len, argv[0], &enum_value) < 0) {
		return -EINVAL;
	}

	switch (prop->prop) {
	case CHARGER_PROP_ONLINE:
		val->online = enum_value;
		return 0;
	case CHARGER_PROP_STATUS:
		val->status = enum_value;
		return 0;
	case CHARGER_PROP_CHARGE_TYPE:
		val->charge_type = enum_value;
		return 0;
	case CHARGER_PROP_HEALTH:
		val->health = enum_value;
		return 0;
	default:
		return -EINVAL;
	}
}

static void print_enum_charger_prop(const struct shell *sh, const struct charger_shell_prop *prop,
				    const union charger_propval *val)
{
	int v;

	switch (prop->prop) {
	case CHARGER_PROP_ONLINE:
		v = val->online;
		break;
	case CHARGER_PROP_STATUS:
		v = val->status;
		break;
	case CHARGER_PROP_CHARGE_TYPE:
		v = val->charge_type;
		break;
	case CHARGER_PROP_HEALTH:
		v = val->health;
		break;
	default:
		return;
	}

	shell_print(sh, "%s: %s", prop->name,
		    enum_to_name(prop->enum_names, prop->enum_names_len, v));
}

static int parse_current_notifier(size_t argc, char **argv, union charger_propval *val)
{
	int severity;
	struct charger_current_notifier *n = &val->input_current_notification;

	if (argc != 3) {
		return -EINVAL;
	}
	if (enum_from_name(severity_names, ARRAY_SIZE(severity_names), argv[0], &severity) < 0 ||
	    severity > UINT8_MAX) {
		return -EINVAL;
	}
	n->severity = (uint8_t)severity;
	if (uint_from_string(argv[1], &n->current_ua) < 0) {
		return -EINVAL;
	}

	return uint_from_string(argv[2], &n->duration_us);
}

static void print_current_notifier(const struct shell *sh, const struct charger_shell_prop *prop,
				   const union charger_propval *val)
{
	const struct charger_current_notifier *n = &val->input_current_notification;

	shell_print(sh, "%s: severity=%s current_ua=%u duration_us=%u", prop->name,
		    enum_to_name(severity_names, ARRAY_SIZE(severity_names), n->severity),
		    n->current_ua, n->duration_us);
}

static const struct charger_shell_prop *prop_from_name(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(charger_props); i++) {
		if (strcmp(charger_props[i].name, name) == 0) {
			return &charger_props[i];
		}
	}

	return NULL;
}

static int parse_prop_value(const struct charger_shell_prop *prop, size_t argc, char **argv,
			    union charger_propval *val)
{
	int ret = 0;

	switch (prop->type) {
	case CHARGER_SHELL_PROP_ENUM:
		return parse_enum_charger_prop(prop, argc, argv, val);
	case CHARGER_SHELL_PROP_BOOL:
		if (argc != 1) {
			return -EINVAL;
		}
		val->present = shell_strtobool(argv[0], 0, &ret);
		return ret;
	case CHARGER_SHELL_PROP_UINT:
		if (argc != 1) {
			return -EINVAL;
		}
		return uint_from_string(argv[0], &val->custom_uint);
	case CHARGER_SHELL_PROP_CURRENT_NOTIFIER:
		return parse_current_notifier(argc, argv, val);
	}

	return -EINVAL;
}

static void print_prop_value(const struct shell *sh, const struct charger_shell_prop *prop,
			     const union charger_propval *val)
{
	switch (prop->type) {
	case CHARGER_SHELL_PROP_ENUM:
		print_enum_charger_prop(sh, prop, val);
		break;
	case CHARGER_SHELL_PROP_BOOL:
		shell_print(sh, "%s: %s", prop->name, val->present ? "true" : "false");
		break;
	case CHARGER_SHELL_PROP_UINT:
		shell_print(sh, "%s: %u", prop->name, val->custom_uint);
		break;
	case CHARGER_SHELL_PROP_CURRENT_NOTIFIER:
		print_current_notifier(sh, prop, val);
		break;
	}
}

static const struct device *get_charger_device(const struct shell *sh, const char *name)
{
	const struct device *dev = shell_device_get_binding(name);

	if (dev == NULL) {
		shell_error(sh, "Charger device %s not available", name);
		return NULL;
	}

	if (!DEVICE_API_IS(charger, dev)) {
		shell_error(sh, "Device %s is not a charger", name);
		return NULL;
	}

	return dev;
}

static int resolve_charger_and_prop(const struct shell *sh, const char *dev_name,
				    const char *prop_name, const struct device **dev_out,
				    const struct charger_shell_prop **prop_out)
{
	const struct device *dev;
	const struct charger_shell_prop *prop;

	dev = get_charger_device(sh, dev_name);
	if (dev == NULL) {
		return -ENODEV;
	}

	prop = prop_from_name(prop_name);
	if (prop == NULL) {
		shell_error(sh, "Invalid property: %s", prop_name);
		return -EINVAL;
	}

	*dev_out = dev;
	*prop_out = prop;
	return 0;
}

static void charger_shell_prop_io_error(const struct shell *sh, const char *verb,
					const struct charger_shell_prop *prop, int ret)
{
	if (ret == -ENOTSUP) {
		shell_error(sh, "%s not supported for property '%s'", verb, prop->name);
	} else {
		shell_error(sh, "%s failed for property '%s' (%d)", verb, prop->name, ret);
	}
}

static int cmd_get(const struct shell *sh, size_t argc, char **argv)
{
	const struct charger_shell_prop *prop;
	const struct device *dev;
	union charger_propval val = {0};
	int ret;

	ARG_UNUSED(argc);

	ret = resolve_charger_and_prop(sh, argv[1], argv[2], &dev, &prop);
	if (ret < 0) {
		return ret;
	}

	ret = charger_get_prop(dev, prop->prop, &val);
	if (ret < 0) {
		charger_shell_prop_io_error(sh, "Get", prop, ret);
		return ret;
	}

	print_prop_value(sh, prop, &val);
	return 0;
}

static int cmd_set(const struct shell *sh, size_t argc, char **argv)
{
	const struct charger_shell_prop *prop;
	const struct device *dev;
	union charger_propval val = {0};
	int ret;

	ret = resolve_charger_and_prop(sh, argv[1], argv[2], &dev, &prop);
	if (ret < 0) {
		return ret;
	}

	ret = parse_prop_value(prop, argc - 3, &argv[3], &val);
	if (ret < 0) {
		shell_error(sh, "Invalid value for %s", prop->name);
		return ret;
	}

	ret = charger_set_prop(dev, prop->prop, &val);
	if (ret < 0) {
		charger_shell_prop_io_error(sh, "Set", prop, ret);
		return ret;
	}

	return 0;
}

static int cmd_charge_enable_toggle(const struct shell *sh, size_t argc, char **argv, bool enable)
{
	const struct device *dev;
	int ret;

	ARG_UNUSED(argc);

	dev = get_charger_device(sh, argv[1]);
	if (dev == NULL) {
		return -ENODEV;
	}

	ret = charger_charge_enable(dev, enable);
	if (ret < 0) {
		shell_error(sh, "Failed to %s charging (%d)", enable ? "enable" : "disable", ret);
		return ret;
	}

	return 0;
}

static int cmd_enable(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_charge_enable_toggle(sh, argc, argv, true);
}

static int cmd_disable(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_charge_enable_toggle(sh, argc, argv, false);
}

static bool device_is_charger(const struct device *dev)
{
	return DEVICE_API_IS(charger, dev);
}

static void device_name_get(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_filter(idx, device_is_charger);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_device_name, device_name_get);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_charger_cmds,
	SHELL_CMD_ARG(get, &dsub_device_name,
		      SHELL_HELP("Get charger property", "<device> <property>"), cmd_get, 3, 0),
	SHELL_CMD_ARG(set, &dsub_device_name,
		      SHELL_HELP("Set charger property", "<device> <property> <value...>"), cmd_set,
		      4, 2),
	SHELL_CMD_ARG(enable, &dsub_device_name, SHELL_HELP("Enable charging", "<device>"),
		      cmd_enable, 2, 0),
	SHELL_CMD_ARG(disable, &dsub_device_name, SHELL_HELP("Disable charging", "<device>"),
		      cmd_disable, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(charger, &sub_charger_cmds, "Battery charger commands", NULL);
