/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_USB_DEVICE_STACK
#include <zephyr/usb/usb_device.h>
#endif

#include "ocpp_demo.h"

LOG_MODULE_REGISTER(usb_status, LOG_LEVEL_INF);

static bool usb_is_connected;

#ifdef CONFIG_USB_DEVICE_STACK
static void usb_dc_status_cb(enum usb_dc_status_code status, const uint8_t *param)
{
	ARG_UNUSED(param);
	
	switch (status) {
	case USB_DC_ERROR:
		LOG_ERR("USB device error");
		usb_is_connected = false;
		break;
	case USB_DC_RESET:
		LOG_INF("USB device reset");
		usb_is_connected = false;
		break;
	case USB_DC_CONNECTED:
		LOG_INF("USB device connected");
		usb_is_connected = true;
		break;
	case USB_DC_CONFIGURED:
		LOG_INF("USB device configured");
		usb_is_connected = true;
		break;
	case USB_DC_DISCONNECTED:
		LOG_INF("USB device disconnected");
		usb_is_connected = false;
		break;
	case USB_DC_SUSPEND:
		LOG_INF("USB device suspended");
		break;
	case USB_DC_RESUME:
		LOG_INF("USB device resumed");
		break;
	default:
		LOG_WRN("USB unknown status: %d", status);
		break;
	}
}
#endif

void usb_status_init(void)
{
#ifdef CONFIG_USB_DEVICE_STACK
	int ret;
	
	LOG_INF("Initializing USB device stack (Next generation)");
	
	ret = usb_enable(usb_dc_status_cb);
	if (ret != 0) {
		LOG_ERR("Failed to enable USB device: %d", ret);
		usb_is_connected = false;
		return;
	}
	
	LOG_INF("USB device stack initialized successfully");
	usb_is_connected = false; /* Will be set by callback */
#else
	LOG_WRN("USB device stack not available (CONFIG_USB_DEVICE_STACK not enabled)");
	usb_is_connected = false;
#endif
}

bool usb_status_is_connected(void)
{
	return usb_is_connected;
}
