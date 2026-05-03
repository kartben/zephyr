/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/shell/shell.h>

/* Describes how a property value is stored in union fuel_gauge_prop_val */
enum fg_prop_fmt {
	FG_FMT_BOOL,
	FG_FMT_INT_UV,       /* signed int, raw in µV, displayed as mV */
	FG_FMT_INT_UA,       /* signed int, raw in µA, displayed as mA */
	FG_FMT_UINT8_PCT,    /* uint8_t, percent 0-100 */
	FG_FMT_UINT16_DK,    /* uint16_t, 0.1 K, displayed as °C */
	FG_FMT_UINT16_MAH,   /* uint16_t, mAh */
	FG_FMT_UINT16_MV,    /* uint16_t, mV */
	FG_FMT_UINT16_HEX,   /* uint16_t, hex flags */
	FG_FMT_UINT32_CNT,   /* uint32_t, plain count */
	FG_FMT_UINT32_MIN,   /* uint32_t, minutes */
	FG_FMT_UINT32_UAH,   /* uint32_t, raw in µAh, displayed as mAh */
	FG_FMT_UINT32_UA,    /* uint32_t, raw in µA, displayed as mA */
	FG_FMT_UINT32_UV,    /* uint32_t, raw in µV, displayed as mV */
	FG_FMT_UINT32_HEX,   /* uint32_t, hex flags */
	FG_FMT_STR,          /* buffer property: length-prefixed string */
};

struct fg_prop_desc {
	const char *name;
	fuel_gauge_prop_t prop;
	uint8_t fmt;
};

/* clang-format off */
static const struct fg_prop_desc fg_props[] = {
	{"voltage",              FUEL_GAUGE_VOLTAGE,                  FG_FMT_INT_UV},
	{"current",              FUEL_GAUGE_CURRENT,                  FG_FMT_INT_UA},
	{"avg_current",          FUEL_GAUGE_AVG_CURRENT,              FG_FMT_INT_UA},
	{"relative_soc",         FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE, FG_FMT_UINT8_PCT},
	{"absolute_soc",         FUEL_GAUGE_ABSOLUTE_STATE_OF_CHARGE, FG_FMT_UINT8_PCT},
	{"state_of_health",      FUEL_GAUGE_STATE_OF_HEALTH,          FG_FMT_UINT8_PCT},
	{"temperature",          FUEL_GAUGE_TEMPERATURE,              FG_FMT_UINT16_DK},
	{"remaining_capacity",   FUEL_GAUGE_REMAINING_CAPACITY,       FG_FMT_UINT32_UAH},
	{"full_charge_capacity", FUEL_GAUGE_FULL_CHARGE_CAPACITY,     FG_FMT_UINT32_UAH},
	{"design_capacity",      FUEL_GAUGE_DESIGN_CAPACITY,          FG_FMT_UINT16_MAH},
	{"design_voltage",       FUEL_GAUGE_DESIGN_VOLTAGE,           FG_FMT_UINT16_MV},
	{"charge_current",       FUEL_GAUGE_CHARGE_CURRENT,           FG_FMT_UINT32_UA},
	{"charge_voltage",       FUEL_GAUGE_CHARGE_VOLTAGE,           FG_FMT_UINT32_UV},
	{"runtime_to_empty",     FUEL_GAUGE_RUNTIME_TO_EMPTY,         FG_FMT_UINT32_MIN},
	{"runtime_to_full",      FUEL_GAUGE_RUNTIME_TO_FULL,          FG_FMT_UINT32_MIN},
	{"cycle_count",          FUEL_GAUGE_CYCLE_COUNT,              FG_FMT_UINT32_CNT},
	{"present_state",        FUEL_GAUGE_PRESENT_STATE,            FG_FMT_BOOL},
	{"charge_cutoff",        FUEL_GAUGE_CHARGE_CUTOFF,            FG_FMT_BOOL},
	{"status",               FUEL_GAUGE_STATUS,                   FG_FMT_UINT16_HEX},
	{"flags",                FUEL_GAUGE_FLAGS,                    FG_FMT_UINT32_HEX},
	{"connect_state",        FUEL_GAUGE_CONNECT_STATE,            FG_FMT_UINT32_HEX},
	{"manufacturer_name",    FUEL_GAUGE_MANUFACTURER_NAME,        FG_FMT_STR},
	{"device_name",          FUEL_GAUGE_DEVICE_NAME,              FG_FMT_STR},
	{"device_chemistry",     FUEL_GAUGE_DEVICE_CHEMISTRY,         FG_FMT_STR},
};
/* clang-format on */

static const struct fg_prop_desc *fg_prop_lookup(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(fg_props); i++) {
		if (strcmp(fg_props[i].name, name) == 0) {
			return &fg_props[i];
		}
	}
	return NULL;
}

/*
 * Print a property value.
 *
 * All union members of the same C type share the same storage at offset 0 with
 * identical representation.  Accessing a uint32_t member after writing any
 * other uint32_t member (and similarly for int, uint16_t, uint8_t, bool) is
 * therefore valid and yields the same value.  The member names used below are
 * chosen as canonical representatives for each C type.
 */
static void fg_print_value(const struct shell *sh, const char *dev_name,
			   const char *prop_name, const struct fg_prop_desc *desc,
			   const union fuel_gauge_prop_val *val)
{
	switch ((enum fg_prop_fmt)desc->fmt) {

	case FG_FMT_INT_UV: {
		/* val->voltage is the canonical int accessor for signed-int properties */
		int uv = val->voltage;
		int abs_uv = (uv < 0) ? -uv : uv;

		shell_print(sh, "%s: %s = %s%d.%03d mV",
			    dev_name, prop_name, (uv < 0) ? "-" : "",
			    abs_uv / 1000, abs_uv % 1000);
		break;
	}

	case FG_FMT_INT_UA: {
		/* val->current is the canonical int accessor; avg_current aliases it */
		int ua = val->current;
		int abs_ua = (ua < 0) ? -ua : ua;

		shell_print(sh, "%s: %s = %s%d.%03d mA",
			    dev_name, prop_name, (ua < 0) ? "-" : "",
			    abs_ua / 1000, abs_ua % 1000);
		break;
	}

	case FG_FMT_UINT8_PCT:
		/* val->relative_state_of_charge is the canonical uint8_t accessor */
		shell_print(sh, "%s: %s = %u %%",
			    dev_name, prop_name,
			    (unsigned int)val->relative_state_of_charge);
		break;

	case FG_FMT_UINT16_DK: {
		/*
		 * val->temperature is the canonical uint16_t accessor.
		 * Convert decidegrees Kelvin (0.1 K) to Celsius:
		 *   T_C = (raw - 2732) * 0.1 °C   (2732 ≈ 273.2 K = 0 °C)
		 */
		int32_t cdec = (int32_t)val->temperature - 2732;
		bool neg = (cdec < 0);
		uint32_t abs_cdec = (uint32_t)(neg ? -cdec : cdec);

		shell_print(sh, "%s: %s = %s%u.%u °C",
			    dev_name, prop_name, neg ? "-" : "",
			    abs_cdec / 10U, abs_cdec % 10U);
		break;
	}

	case FG_FMT_UINT16_MAH:
		shell_print(sh, "%s: %s = %u mAh",
			    dev_name, prop_name, (unsigned int)val->temperature);
		break;

	case FG_FMT_UINT16_MV:
		shell_print(sh, "%s: %s = %u mV",
			    dev_name, prop_name, (unsigned int)val->temperature);
		break;

	case FG_FMT_UINT16_HEX:
		shell_print(sh, "%s: %s = 0x%04x",
			    dev_name, prop_name, (unsigned int)val->temperature);
		break;

	case FG_FMT_UINT32_CNT:
		/* val->remaining_capacity is the canonical uint32_t accessor */
		shell_print(sh, "%s: %s = %u",
			    dev_name, prop_name, (unsigned int)val->remaining_capacity);
		break;

	case FG_FMT_UINT32_MIN:
		shell_print(sh, "%s: %s = %u min",
			    dev_name, prop_name, (unsigned int)val->remaining_capacity);
		break;

	case FG_FMT_UINT32_UAH:
		shell_print(sh, "%s: %s = %u uAh (%u mAh)",
			    dev_name, prop_name,
			    (unsigned int)val->remaining_capacity,
			    (unsigned int)(val->remaining_capacity / 1000U));
		break;

	case FG_FMT_UINT32_UA:
		shell_print(sh, "%s: %s = %u uA (%u mA)",
			    dev_name, prop_name,
			    (unsigned int)val->remaining_capacity,
			    (unsigned int)(val->remaining_capacity / 1000U));
		break;

	case FG_FMT_UINT32_UV:
		shell_print(sh, "%s: %s = %u uV (%u mV)",
			    dev_name, prop_name,
			    (unsigned int)val->remaining_capacity,
			    (unsigned int)(val->remaining_capacity / 1000U));
		break;

	case FG_FMT_UINT32_HEX:
		shell_print(sh, "%s: %s = 0x%08x",
			    dev_name, prop_name, (unsigned int)val->remaining_capacity);
		break;

	case FG_FMT_BOOL:
		/* val->present_state is the canonical bool accessor */
		shell_print(sh, "%s: %s = %s",
			    dev_name, prop_name,
			    val->present_state ? "true" : "false");
		break;

	default:
		break;
	}
}

static int cmd_get_prop(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	const struct fg_prop_desc *desc;
	int ret;

	ARG_UNUSED(argc);

	dev = shell_device_get_binding(argv[1]);
	if (dev == NULL) {
		shell_error(sh, "Fuel gauge device %s not found", argv[1]);
		return -ENODEV;
	}

	desc = fg_prop_lookup(argv[2]);
	if (desc == NULL) {
		shell_error(sh, "Unknown property: %s", argv[2]);
		return -EINVAL;
	}

	if (desc->fmt == FG_FMT_STR) {
		/*
		 * Buffer string properties (manufacturer_name, device_name,
		 * device_chemistry) use a length-prefixed format: buf[0] is the
		 * length and buf[1..length] is the string data.
		 *
		 * The largest string property is manufacturer_name (20 chars).
		 * Allocate one extra byte beyond the maximum data length for
		 * NUL-termination.
		 */
		uint8_t str_buf[1U + SBS_GAUGE_MANUFACTURER_NAME_MAX_SIZE + 1U];
		uint8_t data_len;

		memset(str_buf, 0, sizeof(str_buf));
		ret = fuel_gauge_get_buffer_prop(dev, desc->prop,
						 str_buf, sizeof(str_buf) - 1U);
		if (ret < 0) {
			shell_error(sh, "%s: %s failed (%d)", argv[1], argv[2], ret);
			return ret;
		}

		data_len = str_buf[0];
		if (data_len >= sizeof(str_buf) - 1U) {
			data_len = (uint8_t)(sizeof(str_buf) - 2U);
		}
		str_buf[1U + data_len] = '\0';
		shell_print(sh, "%s: %s = %s", argv[1], argv[2],
			    (const char *)&str_buf[1]);
	} else {
		union fuel_gauge_prop_val val;

		ret = fuel_gauge_get_prop(dev, desc->prop, &val);
		if (ret < 0) {
			shell_error(sh, "%s: %s failed (%d)", argv[1], argv[2], ret);
			return ret;
		}

		fg_print_value(sh, argv[1], argv[2], desc, &val);
	}

	return 0;
}

static int cmd_battery_cutoff(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	int ret;

	ARG_UNUSED(argc);

	dev = shell_device_get_binding(argv[1]);
	if (dev == NULL) {
		shell_error(sh, "Fuel gauge device %s not found", argv[1]);
		return -ENODEV;
	}

	ret = fuel_gauge_battery_cutoff(dev);
	if (ret < 0) {
		shell_error(sh, "%s: battery cutoff failed (%d)", argv[1], ret);
		return ret;
	}

	shell_print(sh, "%s: battery cutoff initiated", argv[1]);
	return 0;
}

static bool device_is_fuel_gauge(const struct device *dev)
{
	return DEVICE_API_IS(fuel_gauge, dev);
}

static void dsub_fg_prop_name(size_t idx, struct shell_static_entry *entry)
{
	entry->syntax = (idx < ARRAY_SIZE(fg_props)) ? fg_props[idx].name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_fg_prop, dsub_fg_prop_name);

static void dsub_fg_dev_get_prop(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_filter(idx, device_is_fuel_gauge);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = &dsub_fg_prop;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_fg_dev_prop, dsub_fg_dev_get_prop);

static void dsub_fg_dev(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_filter(idx, device_is_fuel_gauge);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_fg_device, dsub_fg_dev);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_fuel_gauge,
	SHELL_CMD_ARG(get_prop, &dsub_fg_dev_prop,
		      SHELL_HELP("Read a fuel gauge property",
				 "<device> <property>"),
		      cmd_get_prop, 3, 0),
	SHELL_CMD_ARG(battery_cutoff, &dsub_fg_device,
		      SHELL_HELP("Cut off the battery", "<device>"),
		      cmd_battery_cutoff, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(fuel_gauge, &sub_fuel_gauge, "Fuel gauge shell commands", NULL);
