/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/shell/shell.h>
#include <zephyr/audio/dmic.h>

#define DMIC_TRIGGER_HELP                                                                           \
	SHELL_HELP("Send a trigger command to a DMIC device",                                      \
		   "<device> <start|stop|pause|release|reset>")

static const char *const dmic_trigger_name[] = {
	[DMIC_TRIGGER_STOP] = "stop",
	[DMIC_TRIGGER_START] = "start",
	[DMIC_TRIGGER_PAUSE] = "pause",
	[DMIC_TRIGGER_RELEASE] = "release",
	[DMIC_TRIGGER_RESET] = "reset",
};

struct args_index {
	uint8_t device;
	uint8_t trigger;
};

static const struct args_index args_indx = {
	.device = 1,
	.trigger = 2,
};

static int cmd_trigger(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	int trigger = -1;

	dev = shell_device_get_binding(argv[args_indx.device]);
	if (!dev) {
		shell_error(sh, "DMIC device not found");
		return -ENODEV;
	}

	for (int i = 0; i < ARRAY_SIZE(dmic_trigger_name); i++) {
		if (strcmp(argv[args_indx.trigger], dmic_trigger_name[i]) == 0) {
			trigger = i;
			break;
		}
	}

	if (trigger < 0) {
		shell_error(sh, "Unknown trigger '%s'", argv[args_indx.trigger]);
		return -EINVAL;
	}

	return dmic_trigger(dev, (enum dmic_trigger)trigger);
}

/* Device name autocompletion support */
static void device_name_get(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_lookup(idx, NULL);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_device_name, device_name_get);

/* clang-format off */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_dmic,
	SHELL_CMD_ARG(trigger, &dsub_device_name, DMIC_TRIGGER_HELP, cmd_trigger,
			3, 0),
	SHELL_SUBCMD_SET_END
);
/* clang-format on */

SHELL_CMD_REGISTER(dmic, &sub_dmic, "Digital Microphone commands", NULL);
