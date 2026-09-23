/*
 * Copyright (c) 2022-2023 Jamie McCrae
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/auxdisplay.h>
#include <zephyr/internal/syscall_handler.h>

static inline int z_vrfy_auxdisplay_display_on(const struct device *dev)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, display_on));
	return z_impl_auxdisplay_display_on(dev);
}
#include <zephyr/syscalls/auxdisplay_display_on_mrsh.c>

static inline int z_vrfy_auxdisplay_display_off(const struct device *dev)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, display_off));
	return z_impl_auxdisplay_display_off(dev);
}
#include <zephyr/syscalls/auxdisplay_display_off_mrsh.c>

static inline int z_vrfy_auxdisplay_cursor_set_enabled(const struct device *dev, bool enabled)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, cursor_set_enabled));
	return z_impl_auxdisplay_cursor_set_enabled(dev, enabled);
}
#include <zephyr/syscalls/auxdisplay_cursor_set_enabled_mrsh.c>

static inline int z_vrfy_auxdisplay_position_blinking_set_enabled(const struct device *dev,
								  bool enabled)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, position_blinking_set_enabled));
	return z_impl_auxdisplay_position_blinking_set_enabled(dev, enabled);
}
#include <zephyr/syscalls/auxdisplay_position_blinking_set_enabled_mrsh.c>

static inline int z_vrfy_auxdisplay_cursor_shift_set(const struct device *dev, uint8_t direction,
						     bool display_shift)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, cursor_shift_set));
	return z_impl_auxdisplay_cursor_shift_set(dev, direction, display_shift);
}
#include <zephyr/syscalls/auxdisplay_cursor_shift_set_mrsh.c>

static inline int z_vrfy_auxdisplay_cursor_position_set(const struct device *dev,
							enum auxdisplay_position type,
							int16_t x, int16_t y)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, cursor_position_set));
	return z_impl_auxdisplay_cursor_position_set(dev, type, x, y);
}
#include <zephyr/syscalls/auxdisplay_cursor_position_set_mrsh.c>

static inline int z_vrfy_auxdisplay_cursor_position_get(const struct device *dev, int16_t *x,
							int16_t *y)
{
	int16_t k_x = 0;
	int16_t k_y = 0;
	int ret;

	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, cursor_position_get));
	ret = z_impl_auxdisplay_cursor_position_get(dev, &k_x, &k_y);
	K_OOPS(k_usermode_to_copy(x, &k_x, sizeof(*x)));
	K_OOPS(k_usermode_to_copy(y, &k_y, sizeof(*y)));
	return ret;
}
#include <zephyr/syscalls/auxdisplay_cursor_position_get_mrsh.c>

static inline int z_vrfy_auxdisplay_display_position_set(const struct device *dev,
							 enum auxdisplay_position type,
							 int16_t x, int16_t y)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, display_position_set));
	return z_impl_auxdisplay_display_position_set(dev, type, x, y);
}
#include <zephyr/syscalls/auxdisplay_display_position_set_mrsh.c>

static inline int z_vrfy_auxdisplay_display_position_get(const struct device *dev, int16_t *x,
							 int16_t *y)
{
	int16_t k_x = 0;
	int16_t k_y = 0;
	int ret;

	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, display_position_get));
	ret = z_impl_auxdisplay_display_position_get(dev, &k_x, &k_y);
	K_OOPS(k_usermode_to_copy(x, &k_x, sizeof(*x)));
	K_OOPS(k_usermode_to_copy(y, &k_y, sizeof(*y)));
	return ret;
}
#include <zephyr/syscalls/auxdisplay_display_position_get_mrsh.c>

static inline int z_vrfy_auxdisplay_capabilities_get(const struct device *dev,
						struct auxdisplay_capabilities *capabilities)
{
	struct auxdisplay_capabilities caps = { 0 };
	int ret;

	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, capabilities_get));
	ret = z_impl_auxdisplay_capabilities_get(dev, &caps);
	K_OOPS(k_usermode_to_copy(capabilities, &caps, sizeof(*capabilities)));
	return ret;
}
#include <zephyr/syscalls/auxdisplay_capabilities_get_mrsh.c>

static inline int z_vrfy_auxdisplay_clear(const struct device *dev)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, clear));
	return z_impl_auxdisplay_clear(dev);
}
#include <zephyr/syscalls/auxdisplay_clear_mrsh.c>

static inline int z_vrfy_auxdisplay_brightness_get(const struct device *dev,
						   uint8_t *brightness)
{
	uint8_t k_brightness = 0;
	int ret;

	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, brightness_get));
	ret = z_impl_auxdisplay_brightness_get(dev, &k_brightness);
	K_OOPS(k_usermode_to_copy(brightness, &k_brightness, sizeof(*brightness)));
	return ret;
}
#include <zephyr/syscalls/auxdisplay_brightness_get_mrsh.c>

static inline int z_vrfy_auxdisplay_brightness_set(const struct device *dev,
						   uint8_t brightness)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, brightness_set));
	return z_impl_auxdisplay_brightness_set(dev, brightness);
}
#include <zephyr/syscalls/auxdisplay_brightness_set_mrsh.c>

static inline int z_vrfy_auxdisplay_backlight_get(const struct device *dev,
						  uint8_t *backlight)
{
	uint8_t k_backlight = 0;
	int ret;

	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, backlight_get));
	ret = z_impl_auxdisplay_backlight_get(dev, &k_backlight);
	K_OOPS(k_usermode_to_copy(backlight, &k_backlight, sizeof(*backlight)));
	return ret;
}
#include <zephyr/syscalls/auxdisplay_backlight_get_mrsh.c>

static inline int z_vrfy_auxdisplay_backlight_set(const struct device *dev,
						  uint8_t backlight)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, backlight_set));
	return z_impl_auxdisplay_backlight_set(dev, backlight);
}
#include <zephyr/syscalls/auxdisplay_backlight_set_mrsh.c>

static inline int z_vrfy_auxdisplay_is_busy(const struct device *dev)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, is_busy));
	return z_impl_auxdisplay_is_busy(dev);
}
#include <zephyr/syscalls/auxdisplay_is_busy_mrsh.c>

static inline int z_vrfy_auxdisplay_custom_character_set(const struct device *dev,
							 struct auxdisplay_character *character)
{
	struct auxdisplay_character character_copy;
	struct auxdisplay_capabilities caps = { 0 };
	int ret;

	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, custom_character_set));
	K_OOPS(k_usermode_from_copy(&character_copy, character, sizeof(character_copy)));

	ret = z_impl_auxdisplay_capabilities_get(dev, &caps);
	if (ret != 0) {
		return ret;
	}

	if ((caps.custom_character_width == 0U) || (caps.custom_character_height == 0U)) {
		return -EINVAL;
	}

	K_OOPS(K_SYSCALL_MEMORY_ARRAY_READ(character_copy.data,
					   caps.custom_character_width,
					   caps.custom_character_height));

	ret = z_impl_auxdisplay_custom_character_set(dev, &character_copy);
	K_OOPS(k_usermode_to_copy(character, &character_copy, sizeof(*character)));
	return ret;
}
#include <zephyr/syscalls/auxdisplay_custom_character_set_mrsh.c>

static inline int z_vrfy_auxdisplay_write(const struct device *dev, const uint8_t *data,
					  uint16_t len)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, write));
	K_OOPS(K_SYSCALL_MEMORY_READ(data, len));
	return z_impl_auxdisplay_write(dev, data, len);
}
#include <zephyr/syscalls/auxdisplay_write_mrsh.c>

static inline int z_vrfy_auxdisplay_custom_command(const struct device *dev,
						   struct auxdisplay_custom_data *data)
{
	struct auxdisplay_custom_data data_copy;

	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, custom_command));
	K_OOPS(k_usermode_from_copy(&data_copy, data, sizeof(data_copy)));
	if (data_copy.len > 0U) {
		K_OOPS(K_SYSCALL_MEMORY_READ(data_copy.data, data_copy.len));
	}

	return z_impl_auxdisplay_custom_command(dev, &data_copy);
}
#include <zephyr/syscalls/auxdisplay_custom_command_mrsh.c>

static inline int z_vrfy_auxdisplay_custom_indicator_set(const struct device *dev,
							 uint8_t index, bool enable)
{
	K_OOPS(K_SYSCALL_DRIVER_AUXDISPLAY(dev, custom_indicator_set));
	return z_impl_auxdisplay_custom_indicator_set(dev, index, enable);
}
#include <zephyr/syscalls/auxdisplay_custom_indicator_set_mrsh.c>
