/*
 * Copyright (c) 2026 Benjamin Cabé <benjamin@zephyrproject.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/drivers/auxdisplay.h>
#include <zephyr/shell/shell.h>

#define AUXD_ARGV_DEVICE  1
#define AUXD_ARGV_PATTERN 2
#define AUXD_ARGV_X       3
#define AUXD_ARGV_Y       4

static int get_auxdisplay_device(const struct shell *sh, const char *name,
				 const struct device **dev)
{
	*dev = shell_device_get_binding(name);
	if (!device_is_ready(*dev)) {
		shell_error(sh, "Auxiliary display device not %s",
			    *dev == NULL ? "found" : "ready");
		return -ENODEV;
	}

	return 0;
}

static int cmd_clear(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	int err;

	ARG_UNUSED(argc);

	err = get_auxdisplay_device(sh, argv[AUXD_ARGV_DEVICE], &dev);
	if (err < 0) {
		return err;
	}

	err = auxdisplay_clear(dev);
	if (err < 0) {
		shell_error(sh, "Failed to clear display (err %d)", err);
		return err;
	}

	return 0;
}

static int cmd_size(const struct shell *sh, size_t argc, char **argv)
{
	struct auxdisplay_capabilities capabilities;
	const struct device *dev;
	int err;

	ARG_UNUSED(argc);

	err = get_auxdisplay_device(sh, argv[AUXD_ARGV_DEVICE], &dev);
	if (err < 0) {
		return err;
	}

	err = auxdisplay_capabilities_get(dev, &capabilities);
	if (err < 0) {
		shell_error(sh, "Failed to get display capabilities (err %d)", err);
		return err;
	}

	shell_print(sh, "%zu rows, %zu columns", capabilities.rows, capabilities.columns);

	return 0;
}

static int cmd_write(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	int16_t x = 0;
	int16_t y = 0;
	size_t len;
	long val;
	int err;

	err = get_auxdisplay_device(sh, argv[AUXD_ARGV_DEVICE], &dev);
	if (err < 0) {
		return err;
	}

	if (argc > AUXD_ARGV_X) {
		err = 0;
		val = shell_strtol(argv[AUXD_ARGV_X], 0, &err);
		if (err < 0 || val < INT16_MIN || val > INT16_MAX) {
			shell_error(sh, "Error parsing X position");
			return -EINVAL;
		}
		x = (int16_t)val;
	}

	if (argc > AUXD_ARGV_Y) {
		err = 0;
		val = shell_strtol(argv[AUXD_ARGV_Y], 0, &err);
		if (err < 0 || val < INT16_MIN || val > INT16_MAX) {
			shell_error(sh, "Error parsing Y position");
			return -EINVAL;
		}
		y = (int16_t)val;
	}

	err = auxdisplay_cursor_position_set(dev, AUXDISPLAY_POSITION_ABSOLUTE, x, y);
	if (err < 0) {
		shell_error(sh, "Failed to set cursor position (err %d)", err);
		return err;
	}

	len = strlen(argv[AUXD_ARGV_PATTERN]);
	if (len > UINT16_MAX) {
		shell_error(sh, "Pattern too long");
		return -EINVAL;
	}

	err = auxdisplay_write(dev, (const uint8_t *)argv[AUXD_ARGV_PATTERN], (uint16_t)len);
	if (err < 0) {
		shell_error(sh, "Failed to write to display (err %d)", err);
		return err;
	}

	return 0;
}

static bool device_is_auxdisplay(const struct device *dev)
{
	return DEVICE_API_IS(auxdisplay, dev);
}

/* Device name autocompletion support */
static void device_name_get(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_filter(idx, device_is_auxdisplay);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_device_name, device_name_get);

/* clang-format off */
SHELL_STATIC_SUBCMD_SET_CREATE(
	auxdisplay_cmds,
	SHELL_CMD_ARG(clear, &dsub_device_name,
		      SHELL_HELP("Clear the auxiliary display", "<device>"),
		      cmd_clear, 2, 0),
	SHELL_CMD_ARG(size, &dsub_device_name,
		      SHELL_HELP("Show auxiliary display size", "<device>"),
		      cmd_size, 2, 0),
	SHELL_CMD_ARG(write, &dsub_device_name,
		      SHELL_HELP("Write text to the auxiliary display",
				 "<device> <pattern> [x] [y]"),
		      cmd_write, 3, 2),
	SHELL_SUBCMD_SET_END
);
/* clang-format on */

SHELL_CMD_REGISTER(auxdisplay, &auxdisplay_cmds, "Auxiliary display shell commands", NULL);
