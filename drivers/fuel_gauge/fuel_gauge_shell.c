/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>

enum fuel_gauge_prop_fmt {
	FG_FMT_INT_UA,      /* signed int, uA (avg_current / current) */
	FG_FMT_INT_UV,      /* signed int, uV (voltage) */
	FG_FMT_UINT32_UAH,  /* uint32_t, uAh (remaining/full charge capacity) */
	FG_FMT_UINT32_UA,   /* uint32_t, uA (chg_current) */
	FG_FMT_UINT32_UV,   /* uint32_t, uV (chg_voltage) */
	FG_FMT_UINT32_MIN,  /* uint32_t, minutes (runtime_to_*) */
	FG_FMT_UINT32_DEC,  /* uint32_t, decimal (cycle_count) */
	FG_FMT_UINT32_HEX,  /* uint32_t, hex (flags, connect_state) */
	FG_FMT_UINT16_MAH,  /* uint16_t, mAh (design_cap) */
	FG_FMT_UINT16_MV,   /* uint16_t, mV (design_volt) */
	FG_FMT_UINT16_HEX,  /* uint16_t, hex (fg_status) */
	FG_FMT_UINT16_01K,  /* uint16_t, 0.1 K - displayed as Celsius + raw */
	FG_FMT_UINT8_PCT,   /* uint8_t, percent (SOC, SOH) */
	FG_FMT_BOOL,        /* bool (present_state, cutoff) */
	FG_FMT_BUFFER_MFR,  /* struct sbs_gauge_manufacturer_name */
	FG_FMT_BUFFER_DEV,  /* struct sbs_gauge_device_name */
	FG_FMT_BUFFER_CHEM, /* struct sbs_gauge_device_chemistry */
};

struct fuel_gauge_prop_entry {
	const char *name;
	fuel_gauge_prop_t prop;
	enum fuel_gauge_prop_fmt fmt;
};

/* Sorted alphabetically by shell name for consistent tab-completion ordering */
static const struct fuel_gauge_prop_entry prop_table[] = {
	{"abs-state-of-charge", FUEL_GAUGE_ABSOLUTE_STATE_OF_CHARGE, FG_FMT_UINT8_PCT},
	{"avg-current", FUEL_GAUGE_AVG_CURRENT, FG_FMT_INT_UA},
	{"charge-current", FUEL_GAUGE_CHARGE_CURRENT, FG_FMT_UINT32_UA},
	{"charge-cutoff", FUEL_GAUGE_CHARGE_CUTOFF, FG_FMT_BOOL},
	{"charge-voltage", FUEL_GAUGE_CHARGE_VOLTAGE, FG_FMT_UINT32_UV},
	{"connect-state", FUEL_GAUGE_CONNECT_STATE, FG_FMT_UINT32_HEX},
	{"current", FUEL_GAUGE_CURRENT, FG_FMT_INT_UA},
	{"cycle-count", FUEL_GAUGE_CYCLE_COUNT, FG_FMT_UINT32_DEC},
	{"design-capacity", FUEL_GAUGE_DESIGN_CAPACITY, FG_FMT_UINT16_MAH},
	{"design-voltage", FUEL_GAUGE_DESIGN_VOLTAGE, FG_FMT_UINT16_MV},
	{"device-chemistry", FUEL_GAUGE_DEVICE_CHEMISTRY, FG_FMT_BUFFER_CHEM},
	{"device-name", FUEL_GAUGE_DEVICE_NAME, FG_FMT_BUFFER_DEV},
	{"flags", FUEL_GAUGE_FLAGS, FG_FMT_UINT32_HEX},
	{"full-charge-capacity", FUEL_GAUGE_FULL_CHARGE_CAPACITY, FG_FMT_UINT32_UAH},
	{"manufacturer-name", FUEL_GAUGE_MANUFACTURER_NAME, FG_FMT_BUFFER_MFR},
	{"present-state", FUEL_GAUGE_PRESENT_STATE, FG_FMT_BOOL},
	{"rel-state-of-charge", FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE, FG_FMT_UINT8_PCT},
	{"remaining-capacity", FUEL_GAUGE_REMAINING_CAPACITY, FG_FMT_UINT32_UAH},
	{"runtime-to-empty", FUEL_GAUGE_RUNTIME_TO_EMPTY, FG_FMT_UINT32_MIN},
	{"runtime-to-full", FUEL_GAUGE_RUNTIME_TO_FULL, FG_FMT_UINT32_MIN},
	{"state-of-health", FUEL_GAUGE_STATE_OF_HEALTH, FG_FMT_UINT8_PCT},
	{"status", FUEL_GAUGE_STATUS, FG_FMT_UINT16_HEX},
	{"temperature", FUEL_GAUGE_TEMPERATURE, FG_FMT_UINT16_01K},
	{"voltage", FUEL_GAUGE_VOLTAGE, FG_FMT_INT_UV},
};

static const struct fuel_gauge_prop_entry *find_prop_by_name(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(prop_table); i++) {
		if (strcmp(prop_table[i].name, name) == 0) {
			return &prop_table[i];
		}
	}
	return NULL;
}

static bool is_buffer_prop(enum fuel_gauge_prop_fmt fmt)
{
	return fmt == FG_FMT_BUFFER_MFR || fmt == FG_FMT_BUFFER_DEV || fmt == FG_FMT_BUFFER_CHEM;
}

/* Forward declarations for dynamic command callbacks */
static void dsub_property_name_get(size_t idx, struct shell_static_entry *entry);
SHELL_DYNAMIC_CMD_CREATE(dsub_property_name, dsub_property_name_get);

static void dsub_fg_device_get(size_t idx, struct shell_static_entry *entry);
SHELL_DYNAMIC_CMD_CREATE(dsub_fg_device_with_prop, dsub_fg_device_get);

static void dsub_fg_device_only_get(size_t idx, struct shell_static_entry *entry);
SHELL_DYNAMIC_CMD_CREATE(dsub_fg_device_only, dsub_fg_device_only_get);

static void dsub_property_name_get(size_t idx, struct shell_static_entry *entry)
{
	entry->syntax = (idx < ARRAY_SIZE(prop_table)) ? prop_table[idx].name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

static bool fuel_gauge_device_check(const struct device *dev)
{
	return DEVICE_API_IS(fuel_gauge, dev);
}

static void dsub_fg_device_get(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_filter(idx, fuel_gauge_device_check);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = &dsub_property_name;
}

static void dsub_fg_device_only_get(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_filter(idx, fuel_gauge_device_check);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

static int get_fuel_gauge_device(const struct shell *sh, const char *name,
				 const struct device **dev)
{
	const struct device *d = shell_device_get_binding(name);

	if (d == NULL) {
		shell_error(sh, "Fuel gauge device '%s' not found", name);
		return -ENODEV;
	}
	if (!DEVICE_API_IS(fuel_gauge, d)) {
		shell_error(sh, "Device '%s' is not a fuel gauge", name);
		return -ENODEV;
	}
	*dev = d;
	return 0;
}

static void print_prop_val(const struct shell *sh, const char *prop_name,
			   enum fuel_gauge_prop_fmt fmt, const union fuel_gauge_prop_val *val)
{
	/*
	 * All int-typed union fields alias the same memory, as do all uint32_t
	 * fields, etc. Using one representative field per type is valid C.
	 */
	switch (fmt) {
	case FG_FMT_INT_UA:
		shell_print(sh, "%s: %d uA", prop_name, val->current);
		break;
	case FG_FMT_INT_UV:
		shell_print(sh, "%s: %d uV", prop_name, val->voltage);
		break;
	case FG_FMT_UINT32_UAH:
		shell_print(sh, "%s: %u uAh", prop_name, val->full_charge_capacity);
		break;
	case FG_FMT_UINT32_UA:
		shell_print(sh, "%s: %u uA", prop_name, val->chg_current);
		break;
	case FG_FMT_UINT32_UV:
		shell_print(sh, "%s: %u uV", prop_name, val->chg_voltage);
		break;
	case FG_FMT_UINT32_MIN:
		shell_print(sh, "%s: %u min", prop_name, val->runtime_to_empty);
		break;
	case FG_FMT_UINT32_DEC:
		shell_print(sh, "%s: %u", prop_name, val->cycle_count);
		break;
	case FG_FMT_UINT32_HEX:
		shell_print(sh, "%s: 0x%08x", prop_name, val->flags);
		break;
	case FG_FMT_UINT16_MAH:
		shell_print(sh, "%s: %u mAh", prop_name, (uint32_t)val->design_cap);
		break;
	case FG_FMT_UINT16_MV:
		shell_print(sh, "%s: %u mV", prop_name, (uint32_t)val->design_volt);
		break;
	case FG_FMT_UINT16_HEX:
		shell_print(sh, "%s: 0x%04x", prop_name, (uint32_t)val->fg_status);
		break;
	case FG_FMT_UINT16_01K: {
		/* T_tenths_C = temperature(0.1K) - 2731; e.g. 2981 -> 25.0 C */
		int32_t t_tenths = (int32_t)val->temperature - 2731;
		int32_t t_whole = t_tenths / 10;
		int32_t t_frac = t_tenths % 10;

		if (t_frac < 0) {
			t_frac = -t_frac;
		}
		shell_print(sh, "%s: %d.%d C (raw: %u x0.1K)", prop_name, t_whole, t_frac,
			    (uint32_t)val->temperature);
		break;
	}
	case FG_FMT_UINT8_PCT:
		shell_print(sh, "%s: %u %%", prop_name, (uint32_t)val->absolute_state_of_charge);
		break;
	case FG_FMT_BOOL:
		shell_print(sh, "%s: %s", prop_name, val->present_state ? "true" : "false");
		break;
	default:
		break;
	}
}

static void print_buffer_prop(const struct shell *sh, const struct device *dev,
			      const struct fuel_gauge_prop_entry *entry)
{
	int ret;

	switch (entry->fmt) {
	case FG_FMT_BUFFER_MFR: {
		struct sbs_gauge_manufacturer_name buf;

		ret = fuel_gauge_get_buffer_prop(dev, entry->prop, &buf, sizeof(buf));
		if (ret == 0) {
			buf.manufacturer_name[SBS_GAUGE_MANUFACTURER_NAME_MAX_SIZE - 1] = '\0';
			shell_print(sh, "%s: %s", entry->name, buf.manufacturer_name);
		} else if (ret == -ENOTSUP || ret == -ENOSYS) {
			shell_print(sh, "%s: <not supported>", entry->name);
		} else {
			shell_error(sh, "%s: error %d", entry->name, ret);
		}
		break;
	}
	case FG_FMT_BUFFER_DEV: {
		struct sbs_gauge_device_name buf;

		ret = fuel_gauge_get_buffer_prop(dev, entry->prop, &buf, sizeof(buf));
		if (ret == 0) {
			buf.device_name[SBS_GAUGE_DEVICE_NAME_MAX_SIZE - 1] = '\0';
			shell_print(sh, "%s: %s", entry->name, buf.device_name);
		} else if (ret == -ENOTSUP || ret == -ENOSYS) {
			shell_print(sh, "%s: <not supported>", entry->name);
		} else {
			shell_error(sh, "%s: error %d", entry->name, ret);
		}
		break;
	}
	case FG_FMT_BUFFER_CHEM: {
		struct sbs_gauge_device_chemistry buf;

		ret = fuel_gauge_get_buffer_prop(dev, entry->prop, &buf, sizeof(buf));
		if (ret == 0) {
			buf.device_chemistry[SBS_GAUGE_DEVICE_CHEMISTRY_MAX_SIZE - 1] = '\0';
			shell_print(sh, "%s: %s", entry->name, buf.device_chemistry);
		} else if (ret == -ENOTSUP || ret == -ENOSYS) {
			shell_print(sh, "%s: <not supported>", entry->name);
		} else {
			shell_error(sh, "%s: error %d", entry->name, ret);
		}
		break;
	}
	default:
		break;
	}
}

static int parse_prop_val(const struct shell *sh, const struct fuel_gauge_prop_entry *entry,
			  const char *val_str, union fuel_gauge_prop_val *out)
{
	int err = 0;

	if (is_buffer_prop(entry->fmt)) {
		shell_error(sh, "Buffer properties cannot be set via shell");
		return -ENOTSUP;
	}

	switch (entry->fmt) {
	case FG_FMT_INT_UA:
		out->current = (int)shell_strtol(val_str, 0, &err);
		break;
	case FG_FMT_INT_UV:
		out->voltage = (int)shell_strtol(val_str, 0, &err);
		break;
	case FG_FMT_UINT32_UAH:
		out->full_charge_capacity = (uint32_t)shell_strtoul(val_str, 0, &err);
		break;
	case FG_FMT_UINT32_UA:
		out->chg_current = (uint32_t)shell_strtoul(val_str, 0, &err);
		break;
	case FG_FMT_UINT32_UV:
		out->chg_voltage = (uint32_t)shell_strtoul(val_str, 0, &err);
		break;
	case FG_FMT_UINT32_MIN:
		out->runtime_to_empty = (uint32_t)shell_strtoul(val_str, 0, &err);
		break;
	case FG_FMT_UINT32_DEC:
		out->cycle_count = (uint32_t)shell_strtoul(val_str, 0, &err);
		break;
	case FG_FMT_UINT32_HEX:
		out->flags = (uint32_t)shell_strtoul(val_str, 0, &err);
		break;
	case FG_FMT_UINT16_MAH:
		out->design_cap = (uint16_t)shell_strtoul(val_str, 0, &err);
		break;
	case FG_FMT_UINT16_MV:
		out->design_volt = (uint16_t)shell_strtoul(val_str, 0, &err);
		break;
	case FG_FMT_UINT16_HEX:
		out->fg_status = (uint16_t)shell_strtoul(val_str, 0, &err);
		break;
	case FG_FMT_UINT16_01K:
		out->temperature = (uint16_t)shell_strtoul(val_str, 0, &err);
		break;
	case FG_FMT_UINT8_PCT:
		out->absolute_state_of_charge = (uint8_t)shell_strtoul(val_str, 0, &err);
		break;
	case FG_FMT_BOOL:
		out->present_state = (bool)shell_strtoul(val_str, 0, &err);
		break;
	default:
		shell_error(sh, "Unknown property format");
		return -EINVAL;
	}

	if (err != 0) {
		shell_error(sh, "Invalid value: %s", val_str);
		return -EINVAL;
	}

	return 0;
}

static int cmd_fuel_gauge_get(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	const struct fuel_gauge_prop_entry *entry;
	union fuel_gauge_prop_val val;
	int ret;

	ARG_UNUSED(argc);

	ret = get_fuel_gauge_device(sh, argv[1], &dev);
	if (ret < 0) {
		return ret;
	}

	entry = find_prop_by_name(argv[2]);
	if (entry == NULL) {
		shell_error(sh, "Unknown property: %s", argv[2]);
		return -EINVAL;
	}

	if (is_buffer_prop(entry->fmt)) {
		print_buffer_prop(sh, dev, entry);
		return 0;
	}

	ret = fuel_gauge_get_prop(dev, entry->prop, &val);
	if (ret == -ENOTSUP || ret == -ENOSYS) {
		shell_error(sh, "Property '%s' not supported by this device", entry->name);
		return ret;
	} else if (ret < 0) {
		shell_error(sh, "Failed to get property '%s': %d", entry->name, ret);
		return ret;
	}

	print_prop_val(sh, entry->name, entry->fmt, &val);
	return 0;
}

static int cmd_fuel_gauge_set(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	const struct fuel_gauge_prop_entry *entry;
	union fuel_gauge_prop_val val;
	int ret;

	ARG_UNUSED(argc);

	ret = get_fuel_gauge_device(sh, argv[1], &dev);
	if (ret < 0) {
		return ret;
	}

	entry = find_prop_by_name(argv[2]);
	if (entry == NULL) {
		shell_error(sh, "Unknown property: %s", argv[2]);
		return -EINVAL;
	}

	ret = parse_prop_val(sh, entry, argv[3], &val);
	if (ret < 0) {
		return ret;
	}

	ret = fuel_gauge_set_prop(dev, entry->prop, val);
	if (ret == -ENOTSUP || ret == -ENOSYS) {
		shell_error(sh, "Property '%s' not supported by this device", entry->name);
		return ret;
	} else if (ret < 0) {
		shell_error(sh, "Failed to set property '%s': %d", entry->name, ret);
		return ret;
	}

	shell_print(sh, "Property '%s' set successfully", entry->name);
	return 0;
}

static int cmd_fuel_gauge_cutoff(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	int ret;

	ARG_UNUSED(argc);

	ret = get_fuel_gauge_device(sh, argv[1], &dev);
	if (ret < 0) {
		return ret;
	}

	ret = fuel_gauge_battery_cutoff(dev);
	if (ret == -ENOSYS) {
		shell_error(sh, "Battery cutoff not supported by this device");
		return ret;
	} else if (ret < 0) {
		shell_error(sh, "Battery cutoff failed: %d", ret);
		return ret;
	}

	shell_print(sh, "Battery cutoff initiated");
	return 0;
}

static int cmd_fuel_gauge_info(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	int ret;

	ARG_UNUSED(argc);

	ret = get_fuel_gauge_device(sh, argv[1], &dev);
	if (ret < 0) {
		return ret;
	}

	for (size_t i = 0; i < ARRAY_SIZE(prop_table); i++) {
		const struct fuel_gauge_prop_entry *entry = &prop_table[i];

		if (is_buffer_prop(entry->fmt)) {
			print_buffer_prop(sh, dev, entry);
			continue;
		}

		union fuel_gauge_prop_val val;

		ret = fuel_gauge_get_prop(dev, entry->prop, &val);
		if (ret == -ENOTSUP || ret == -ENOSYS) {
			continue;
		} else if (ret < 0) {
			shell_error(sh, "%s: error %d", entry->name, ret);
			continue;
		}

		print_prop_val(sh, entry->name, entry->fmt, &val);
	}

	return 0;
}

#define FG_GET_HELP SHELL_HELP("Read a fuel gauge property.", "<device> <property>")
#define FG_SET_HELP                                                                                \
	SHELL_HELP("Write a fuel gauge property.\n"                                                \
		   "Value is a decimal integer; use 0x prefix for hex.\n"                          \
		   "Bool properties accept 0 or 1.",                                               \
		   "<device> <property> <value>")
#define FG_CUTOFF_HELP SHELL_HELP("Trigger battery cutoff (ship/shelf mode).", "<device>")
#define FG_INFO_HELP   SHELL_HELP("Print all readable fuel gauge properties.", "<device>")

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_fuel_gauge,
	SHELL_CMD_ARG(cutoff, &dsub_fg_device_only, FG_CUTOFF_HELP, cmd_fuel_gauge_cutoff, 2, 0),
	SHELL_CMD_ARG(get, &dsub_fg_device_with_prop, FG_GET_HELP, cmd_fuel_gauge_get, 3, 0),
	SHELL_CMD_ARG(info, &dsub_fg_device_only, FG_INFO_HELP, cmd_fuel_gauge_info, 2, 0),
	SHELL_CMD_ARG(set, &dsub_fg_device_with_prop, FG_SET_HELP, cmd_fuel_gauge_set, 4, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(fuel_gauge, &sub_fuel_gauge, "Fuel gauge commands", NULL);
