/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_aoa.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(aa_aoa, CONFIG_SAMPLE_AA_LOG_LEVEL);

/*
 * Android Open Accessory protocol, device side. The head unit acts as the
 * accessory: it asks for the protocol version, sends its identity strings and
 * requests accessory mode, after which the device re-enumerates with Google's
 * accessory vendor and product ids.
 */

#define AOA_REQ_GET_PROTOCOL 51U
#define AOA_REQ_SEND_STRING  52U
#define AOA_REQ_START        53U

#define AOA_PROTOCOL_VERSION 2U
#define AOA_STRING_COUNT     6U

static const char *const aoa_string_names[AOA_STRING_COUNT] = {
	"manufacturer", "model", "description", "version", "uri", "serial",
};

static char aoa_strings[AOA_STRING_COUNT][CONFIG_SAMPLE_AA_AOA_STRING_SIZE];
static struct usbd_context *aoa_ctx;
static struct k_work_delayable switch_work;

static struct net_buf *aoa_get_protocol(const struct usbd_context *const ctx,
					const struct usb_setup_packet *const setup)
{
	struct net_buf *buf;

	if (setup->RequestType.recipient != USB_REQTYPE_RECIPIENT_DEVICE) {
		return NULL;
	}

	buf = usbd_ep_ctrl_data_in_alloc(ctx, MIN(setup->wLength, sizeof(uint16_t)));
	if (buf == NULL) {
		return NULL;
	}

	net_buf_add_le16(buf, AOA_PROTOCOL_VERSION);
	LOG_INF("Accessory protocol version requested");

	return buf;
}

static int aoa_send_string(const struct usbd_context *const ctx,
			   const struct usb_setup_packet *const setup,
			   const struct net_buf *const buf)
{
	uint16_t idx = setup->wIndex;
	size_t len;

	if (setup->RequestType.recipient != USB_REQTYPE_RECIPIENT_DEVICE ||
	    idx >= AOA_STRING_COUNT || setup->wLength > CONFIG_SAMPLE_AA_AOA_STRING_SIZE) {
		return -ENOTSUP;
	}

	if (buf == NULL) {
		/* Accept the data stage */
		return 0;
	}

	len = MIN(buf->len, CONFIG_SAMPLE_AA_AOA_STRING_SIZE - 1U);
	memcpy(aoa_strings[idx], buf->data, len);
	aoa_strings[idx][len] = '\0';
	LOG_INF("Accessory %s: \"%s\"", aoa_string_names[idx], aoa_strings[idx]);

	return 0;
}

static int aoa_start(const struct usbd_context *const ctx,
		     const struct usb_setup_packet *const setup, const struct net_buf *const buf)
{
	if (setup->RequestType.recipient != USB_REQTYPE_RECIPIENT_DEVICE) {
		return -ENOTSUP;
	}

	LOG_INF("Accessory mode requested, re-enumerating");
	(void)k_work_schedule(&switch_work, K_MSEC(CONFIG_SAMPLE_AA_AOA_SWITCH_DELAY_MS));

	return 0;
}

USBD_VREQUEST_DEFINE(aoa_vreq_get_protocol, AOA_REQ_GET_PROTOCOL, aoa_get_protocol, NULL);
USBD_VREQUEST_DEFINE(aoa_vreq_send_string, AOA_REQ_SEND_STRING, NULL, aoa_send_string);
USBD_VREQUEST_DEFINE(aoa_vreq_start, AOA_REQ_START, NULL, aoa_start);

static void switch_work_handler(struct k_work *work)
{
	int ret;

	ARG_UNUSED(work);

	ret = usbd_disable(aoa_ctx);
	if (ret != 0 && ret != -EALREADY) {
		LOG_ERR("Failed to detach (%d)", ret);
		return;
	}

	(void)usbd_device_set_vid(aoa_ctx, CONFIG_SAMPLE_AA_AOA_VID);
	(void)usbd_device_set_pid(aoa_ctx, CONFIG_SAMPLE_AA_AOA_PID);

	k_sleep(K_MSEC(CONFIG_SAMPLE_AA_AOA_SWITCH_DELAY_MS));

	ret = usbd_enable(aoa_ctx);
	if (ret != 0) {
		LOG_ERR("Failed to attach in accessory mode (%d)", ret);
	}
}

int aa_aoa_register(struct usbd_context *uds_ctx)
{
	struct usbd_vreq_node *const nodes[] = {
		&aoa_vreq_get_protocol,
		&aoa_vreq_send_string,
		&aoa_vreq_start,
	};

	aoa_ctx = uds_ctx;
	k_work_init_delayable(&switch_work, switch_work_handler);

	for (size_t i = 0; i < ARRAY_SIZE(nodes); i++) {
		int ret = usbd_device_register_vreq(uds_ctx, nodes[i]);

		if (ret != 0) {
			LOG_ERR("Failed to register vendor request %u (%d)", nodes[i]->code, ret);
			return ret;
		}
	}

	return 0;
}
