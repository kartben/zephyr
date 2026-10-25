/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <zephyr/usb/class/hid.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/usbd.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(hid_kb, LOG_LEVEL_INF);

#include "agent_pad.h"

#define REPORT_SIZE 8

static const uint8_t hid_report_desc[] = HID_KEYBOARD_REPORT_DESC();

static const struct device *hid_dev;
static bool kb_ready;

UDC_STATIC_BUF_DEFINE(report, REPORT_SIZE);
static K_SEM_DEFINE(report_sem, 1, 1);

static void iface_ready(const struct device *dev, const bool ready)
{
	kb_ready = ready;
}

static void input_report_done(const struct device *dev, const uint8_t *const buf)
{
	k_sem_give(&report_sem);
}

static int get_report(const struct device *dev, const uint8_t type, const uint8_t id,
		      const uint16_t len, uint8_t *const buf)
{
	return 0;
}

static int set_report(const struct device *dev, const uint8_t type, const uint8_t id,
		      const uint16_t len, const uint8_t *const buf)
{
	return 0;
}

/*
 * Required because "keyboard" protocol-code declares a boot interface.
 * HID_KEYBOARD_REPORT_DESC() already is the boot layout, so the report
 * format is the same under either protocol and nothing has to change.
 */
static void set_protocol(const struct device *dev, const uint8_t proto)
{
	LOG_DBG("Protocol set to %s", proto == 0U ? "boot" : "report");
}

static const struct hid_device_ops kb_ops = {
	.iface_ready = iface_ready,
	.input_report_done = input_report_done,
	.get_report = get_report,
	.set_report = set_report,
	.set_protocol = set_protocol,
};

static void submit(uint8_t mod, uint8_t code)
{
	memset(report, 0, REPORT_SIZE);
	report[0] = mod;
	report[2] = code;

	if (k_sem_take(&report_sem, K_MSEC(100)) != 0) {
		k_sem_give(&report_sem);
		return;
	}

	if (hid_device_submit_report(hid_dev, REPORT_SIZE, report) != 0) {
		k_sem_give(&report_sem);
	}
}

static void tap(uint8_t mod, uint8_t code)
{
	submit(mod, code);
	submit(0, 0);
}

static bool char_to_tap(char c, uint8_t *mod, uint8_t *code)
{
	static const struct {
		char c;
		uint8_t mod;
		uint8_t code;
	} table[] = {
		{' ', 0, HID_KEY_SPACE},   {'\n', 0, HID_KEY_ENTER},
		{'.', 0, HID_KEY_DOT},	   {',', 0, HID_KEY_COMMA},
		{'-', 0, HID_KEY_MINUS},   {'/', 0, HID_KEY_SLASH},
		{'!', HID_KBD_MODIFIER_LEFT_SHIFT, HID_KEY_1},
		{'#', HID_KBD_MODIFIER_LEFT_SHIFT, HID_KEY_3},
		{'?', HID_KBD_MODIFIER_LEFT_SHIFT, HID_KEY_SLASH},
		{'_', HID_KBD_MODIFIER_LEFT_SHIFT, HID_KEY_MINUS},
	};

	*mod = 0;

	if (c >= 'a' && c <= 'z') {
		*code = HID_KEY_A + (c - 'a');
		return true;
	}
	if (c >= 'A' && c <= 'Z') {
		*mod = HID_KBD_MODIFIER_LEFT_SHIFT;
		*code = HID_KEY_A + (c - 'A');
		return true;
	}
	if (c >= '1' && c <= '9') {
		*code = HID_KEY_1 + (c - '1');
		return true;
	}
	if (c == '0') {
		*code = HID_KEY_0;
		return true;
	}

	for (size_t i = 0; i < ARRAY_SIZE(table); i++) {
		if (table[i].c == c) {
			*mod = table[i].mod;
			*code = table[i].code;
			return true;
		}
	}

	return false;
}

void hid_kb_run(const struct key_action *action)
{
	if (!kb_ready) {
		return;
	}

	if (action->taps != NULL) {
		for (size_t i = 0; i < action->num_taps; i++) {
			tap(action->taps[i].mod, action->taps[i].code);
		}
		return;
	}

	for (const char *c = action->text; *c != '\0'; c++) {
		uint8_t mod, code;

		if (char_to_tap(*c, &mod, &code)) {
			tap(mod, code);
		}
	}
}

int hid_kb_init(void)
{
	struct usbd_context *usbd;
	int ret;

	hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
	if (!device_is_ready(hid_dev)) {
		LOG_ERR("HID device not ready");
		return -EIO;
	}

	ret = hid_device_register(hid_dev, hid_report_desc, sizeof(hid_report_desc), &kb_ops);
	if (ret != 0) {
		LOG_ERR("Failed to register HID device, %d", ret);
		return ret;
	}

	usbd = sample_usbd_init_device(NULL);
	if (usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -EIO;
	}

	if (!usbd_can_detect_vbus(usbd)) {
		ret = usbd_enable(usbd);
		if (ret != 0) {
			LOG_ERR("Failed to enable USB device, %d", ret);
			return ret;
		}
	}

	return 0;
}
