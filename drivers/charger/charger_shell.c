/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/shell/shell.h>

/* Describes how a property value is stored in union charger_propval */
enum chgr_prop_fmt {
	CHGR_FMT_ONLINE,        /* enum charger_online */
	CHGR_FMT_STATUS,        /* enum charger_status */
	CHGR_FMT_CHARGE_TYPE,   /* enum charger_charge_type */
	CHGR_FMT_HEALTH,        /* enum charger_health */
	CHGR_FMT_BOOL,
	CHGR_FMT_UINT32_UA,     /* uint32_t, raw in µA, displayed as mA */
	CHGR_FMT_UINT32_UV,     /* uint32_t, raw in µV, displayed as mV */
};

struct chgr_prop_desc {
	const char *name;
	charger_prop_t prop;
	uint8_t fmt;
	bool writable; /* settable via set_prop */
};

/* clang-format off */
static const struct chgr_prop_desc chgr_props[] = {
	/* name                        prop                                         fmt                    writable */
	{"online",                     CHARGER_PROP_ONLINE,                         CHGR_FMT_ONLINE,       false},
	{"present",                    CHARGER_PROP_PRESENT,                        CHGR_FMT_BOOL,         false},
	{"status",                     CHARGER_PROP_STATUS,                         CHGR_FMT_STATUS,       false},
	{"charge_type",                CHARGER_PROP_CHARGE_TYPE,                    CHGR_FMT_CHARGE_TYPE,  false},
	{"health",                     CHARGER_PROP_HEALTH,                         CHGR_FMT_HEALTH,       false},
	{"constant_charge_current_ua", CHARGER_PROP_CONSTANT_CHARGE_CURRENT_UA,     CHGR_FMT_UINT32_UA,    true},
	{"precharge_current_ua",       CHARGER_PROP_PRECHARGE_CURRENT_UA,           CHGR_FMT_UINT32_UA,    true},
	{"charge_term_current_ua",     CHARGER_PROP_CHARGE_TERM_CURRENT_UA,         CHGR_FMT_UINT32_UA,    true},
	{"constant_charge_voltage_uv", CHARGER_PROP_CONSTANT_CHARGE_VOLTAGE_UV,     CHGR_FMT_UINT32_UV,    true},
	{"input_regulation_current_ua",CHARGER_PROP_INPUT_REGULATION_CURRENT_UA,    CHGR_FMT_UINT32_UA,    true},
	{"input_regulation_voltage_uv",CHARGER_PROP_INPUT_REGULATION_VOLTAGE_UV,    CHGR_FMT_UINT32_UV,    true},
};
/* clang-format on */

static const char *const chgr_online_str[] = {
	[CHARGER_ONLINE_OFFLINE]       = "offline",
	[CHARGER_ONLINE_FIXED]         = "fixed",
	[CHARGER_ONLINE_PROGRAMMABLE]  = "programmable",
};

static const char *const chgr_status_str[] = {
	[CHARGER_STATUS_UNKNOWN]       = "unknown",
	[CHARGER_STATUS_CHARGING]      = "charging",
	[CHARGER_STATUS_DISCHARGING]   = "discharging",
	[CHARGER_STATUS_NOT_CHARGING]  = "not_charging",
	[CHARGER_STATUS_FULL]          = "full",
};

static const char *const chgr_charge_type_str[] = {
	[CHARGER_CHARGE_TYPE_UNKNOWN]  = "unknown",
	[CHARGER_CHARGE_TYPE_NONE]     = "none",
	[CHARGER_CHARGE_TYPE_TRICKLE]  = "trickle",
	[CHARGER_CHARGE_TYPE_FAST]     = "fast",
	[CHARGER_CHARGE_TYPE_STANDARD] = "standard",
	[CHARGER_CHARGE_TYPE_ADAPTIVE] = "adaptive",
	[CHARGER_CHARGE_TYPE_LONGLIFE] = "longlife",
	[CHARGER_CHARGE_TYPE_BYPASS]   = "bypass",
};

static const char *const chgr_health_str[] = {
	[CHARGER_HEALTH_UNKNOWN]               = "unknown",
	[CHARGER_HEALTH_GOOD]                  = "good",
	[CHARGER_HEALTH_OVERHEAT]              = "overheat",
	[CHARGER_HEALTH_OVERVOLTAGE]           = "overvoltage",
	[CHARGER_HEALTH_UNSPEC_FAILURE]        = "unspec_failure",
	[CHARGER_HEALTH_COLD]                  = "cold",
	[CHARGER_HEALTH_WATCHDOG_TIMER_EXPIRE] = "watchdog_timer_expire",
	[CHARGER_HEALTH_SAFETY_TIMER_EXPIRE]   = "safety_timer_expire",
	[CHARGER_HEALTH_CALIBRATION_REQUIRED]  = "calibration_required",
	[CHARGER_HEALTH_WARM]                  = "warm",
	[CHARGER_HEALTH_COOL]                  = "cool",
	[CHARGER_HEALTH_HOT]                   = "hot",
	[CHARGER_HEALTH_NO_BATTERY]            = "no_battery",
};

static const struct chgr_prop_desc *chgr_prop_lookup(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(chgr_props); i++) {
		if (strcmp(chgr_props[i].name, name) == 0) {
			return &chgr_props[i];
		}
	}
	return NULL;
}

static const struct chgr_prop_desc *chgr_writable_prop_lookup(const char *name)
{
	const struct chgr_prop_desc *desc = chgr_prop_lookup(name);

	return (desc != NULL && desc->writable) ? desc : NULL;
}

/*
 * All uint32_t union members share the same storage at offset 0.  Using
 * const_charge_current_ua as the canonical uint32_t accessor is valid for any
 * uint32_t property value.
 */
static void chgr_print_value(const struct shell *sh, const char *dev_name,
			     const char *prop_name, const struct chgr_prop_desc *desc,
			     const union charger_propval *val)
{
	switch ((enum chgr_prop_fmt)desc->fmt) {

	case CHGR_FMT_ONLINE:
		if ((size_t)val->online < ARRAY_SIZE(chgr_online_str)) {
			shell_print(sh, "%s: %s = %s",
				    dev_name, prop_name, chgr_online_str[val->online]);
		} else {
			shell_print(sh, "%s: %s = %d (unknown)",
				    dev_name, prop_name, (int)val->online);
		}
		break;

	case CHGR_FMT_STATUS:
		if ((size_t)val->status < ARRAY_SIZE(chgr_status_str)) {
			shell_print(sh, "%s: %s = %s",
				    dev_name, prop_name, chgr_status_str[val->status]);
		} else {
			shell_print(sh, "%s: %s = %d (unknown)",
				    dev_name, prop_name, (int)val->status);
		}
		break;

	case CHGR_FMT_CHARGE_TYPE:
		if ((size_t)val->charge_type < ARRAY_SIZE(chgr_charge_type_str)) {
			shell_print(sh, "%s: %s = %s",
				    dev_name, prop_name, chgr_charge_type_str[val->charge_type]);
		} else {
			shell_print(sh, "%s: %s = %d (unknown)",
				    dev_name, prop_name, (int)val->charge_type);
		}
		break;

	case CHGR_FMT_HEALTH:
		if ((size_t)val->health < ARRAY_SIZE(chgr_health_str)) {
			shell_print(sh, "%s: %s = %s",
				    dev_name, prop_name, chgr_health_str[val->health]);
		} else {
			shell_print(sh, "%s: %s = %d (unknown)",
				    dev_name, prop_name, (int)val->health);
		}
		break;

	case CHGR_FMT_BOOL:
		shell_print(sh, "%s: %s = %s",
			    dev_name, prop_name, val->present ? "true" : "false");
		break;

	case CHGR_FMT_UINT32_UA:
		shell_print(sh, "%s: %s = %u uA (%u mA)",
			    dev_name, prop_name,
			    (unsigned int)val->const_charge_current_ua,
			    (unsigned int)(val->const_charge_current_ua / 1000U));
		break;

	case CHGR_FMT_UINT32_UV:
		shell_print(sh, "%s: %s = %u uV (%u mV)",
			    dev_name, prop_name,
			    (unsigned int)val->const_charge_current_ua,
			    (unsigned int)(val->const_charge_current_ua / 1000U));
		break;

	default:
		break;
	}
}

static int cmd_get_prop(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	const struct chgr_prop_desc *desc;
	union charger_propval val;
	int ret;

	ARG_UNUSED(argc);

	dev = shell_device_get_binding(argv[1]);
	if (dev == NULL) {
		shell_error(sh, "Charger device %s not found", argv[1]);
		return -ENODEV;
	}

	desc = chgr_prop_lookup(argv[2]);
	if (desc == NULL) {
		shell_error(sh, "Unknown property: %s", argv[2]);
		return -EINVAL;
	}

	ret = charger_get_prop(dev, desc->prop, &val);
	if (ret < 0) {
		shell_error(sh, "%s: %s failed (%d)", argv[1], argv[2], ret);
		return ret;
	}

	chgr_print_value(sh, argv[1], argv[2], desc, &val);
	return 0;
}

static int cmd_set_prop(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	const struct chgr_prop_desc *desc;
	union charger_propval val;
	int ret;

	ARG_UNUSED(argc);

	dev = shell_device_get_binding(argv[1]);
	if (dev == NULL) {
		shell_error(sh, "Charger device %s not found", argv[1]);
		return -ENODEV;
	}

	desc = chgr_writable_prop_lookup(argv[2]);
	if (desc == NULL) {
		if (chgr_prop_lookup(argv[2]) != NULL) {
			shell_error(sh, "%s: read-only property", argv[2]);
		} else {
			shell_error(sh, "Unknown property: %s", argv[2]);
		}
		return -EINVAL;
	}

	switch ((enum chgr_prop_fmt)desc->fmt) {
	case CHGR_FMT_UINT32_UA:
	case CHGR_FMT_UINT32_UV:
		/*
		 * All uint32_t members alias the same storage; write via the
		 * canonical const_charge_current_ua member.
		 */
		val.const_charge_current_ua = (uint32_t)strtoul(argv[3], NULL, 0);
		break;
	default:
		shell_error(sh, "%s: property cannot be set from the shell", argv[2]);
		return -EINVAL;
	}

	ret = charger_set_prop(dev, desc->prop, &val);
	if (ret < 0) {
		shell_error(sh, "%s: set %s failed (%d)", argv[1], argv[2], ret);
		return ret;
	}

	chgr_print_value(sh, argv[1], argv[2], desc, &val);
	return 0;
}

static int cmd_charge_enable(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	bool enable;
	int ret;

	ARG_UNUSED(argc);

	dev = shell_device_get_binding(argv[1]);
	if (dev == NULL) {
		shell_error(sh, "Charger device %s not found", argv[1]);
		return -ENODEV;
	}

	if (strcmp(argv[2], "on") == 0) {
		enable = true;
	} else if (strcmp(argv[2], "off") == 0) {
		enable = false;
	} else {
		shell_error(sh, "Expected 'on' or 'off', got '%s'", argv[2]);
		return -EINVAL;
	}

	ret = charger_charge_enable(dev, enable);
	if (ret < 0) {
		shell_error(sh, "%s: charge_enable failed (%d)", argv[1], ret);
		return ret;
	}

	shell_print(sh, "%s: charging %s", argv[1], enable ? "enabled" : "disabled");
	return 0;
}

static bool device_is_charger(const struct device *dev)
{
	return DEVICE_API_IS(charger, dev);
}

/* Dynamic completions for writable property names */
static void dsub_chgr_writable_prop_name(size_t idx, struct shell_static_entry *entry)
{
	size_t writable_idx = 0;

	entry->syntax = NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;

	for (size_t i = 0; i < ARRAY_SIZE(chgr_props); i++) {
		if (chgr_props[i].writable) {
			if (writable_idx == idx) {
				entry->syntax = chgr_props[i].name;
				return;
			}
			writable_idx++;
		}
	}
}

SHELL_DYNAMIC_CMD_CREATE(dsub_chgr_writable_prop, dsub_chgr_writable_prop_name);

/* Dynamic completions for all property names */
static void dsub_chgr_prop_name(size_t idx, struct shell_static_entry *entry)
{
	entry->syntax = (idx < ARRAY_SIZE(chgr_props)) ? chgr_props[idx].name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_chgr_prop, dsub_chgr_prop_name);

/* on/off completion for charge_enable */
static void dsub_on_off_name(size_t idx, struct shell_static_entry *entry)
{
	static const char *const on_off[] = {"on", "off"};

	entry->syntax = (idx < ARRAY_SIZE(on_off)) ? on_off[idx] : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_on_off, dsub_on_off_name);

/* Device + property chain for get_prop */
static void dsub_chgr_dev_get_prop(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_filter(idx, device_is_charger);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = &dsub_chgr_prop;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_chgr_dev_get, dsub_chgr_dev_get_prop);

/* Device + writable property chain for set_prop */
static void dsub_chgr_dev_set_prop(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_filter(idx, device_is_charger);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = &dsub_chgr_writable_prop;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_chgr_dev_set, dsub_chgr_dev_set_prop);

/* Device + on/off chain for charge_enable */
static void dsub_chgr_dev_enable(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_filter(idx, device_is_charger);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = &dsub_on_off;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_chgr_dev_chg, dsub_chgr_dev_enable);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_charger,
	SHELL_CMD_ARG(charge_enable, &dsub_chgr_dev_chg,
		      SHELL_HELP("Enable or disable charging", "<device> <on|off>"),
		      cmd_charge_enable, 3, 0),
	SHELL_CMD_ARG(get_prop, &dsub_chgr_dev_get,
		      SHELL_HELP("Read a charger property", "<device> <property>"),
		      cmd_get_prop, 3, 0),
	SHELL_CMD_ARG(set_prop, &dsub_chgr_dev_set,
		      SHELL_HELP("Set a charger property", "<device> <property> <value>"),
		      cmd_set_prop, 4, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(charger, &sub_charger, "Charger shell commands", NULL);
