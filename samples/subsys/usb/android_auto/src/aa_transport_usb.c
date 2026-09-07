/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_transport.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/usb/udc.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/usbd.h>

#include <sample_usbd.h>

#include "aa_aoa.h"

LOG_MODULE_REGISTER(aa_usb, CONFIG_SAMPLE_AA_LOG_LEVEL);

/*
 * USB accessory transport: one vendor interface with a bulk OUT and a bulk IN
 * endpoint, exactly what an Android device exposes in accessory mode. Bulk OUT
 * data is treated as a byte stream (hosts do not terminate transfers with
 * short packets) and queued into a pipe read by the protocol thread.
 */

#define OUT_XFER_SIZE CONFIG_SAMPLE_AA_USB_OUT_XFER_SIZE
#define IN_XFER_SIZE  CONFIG_SAMPLE_AA_USB_IN_XFER_SIZE

BUILD_ASSERT(OUT_XFER_SIZE % USBD_MAX_BULK_MPS == 0, "OUT transfers must be MPS multiples");
BUILD_ASSERT(IN_XFER_SIZE % USBD_MAX_BULK_MPS == 0, "IN transfers must be MPS multiples");

UDC_BUF_POOL_DEFINE(aa_out_pool, 2, OUT_XFER_SIZE, sizeof(struct udc_buf_info), NULL);
UDC_BUF_POOL_DEFINE(aa_in_pool, 2, IN_XFER_SIZE, sizeof(struct udc_buf_info), NULL);

K_PIPE_DEFINE(aa_rx_pipe, CONFIG_SAMPLE_AA_USB_RX_PIPE_SIZE, 4);

static K_SEM_DEFINE(link_sem, 0, 1);
static atomic_t link_up;
static struct usbd_context *usbd_ctx;
static struct usbd_class_data *aa_c_data;

/* OUT buffer holding data that did not fit the pipe, re-queued by the reader */
static struct net_buf *pending_out;
static struct k_spinlock pending_lock;

struct aa_usb_desc {
	struct usb_if_descriptor if0;
	struct usb_ep_descriptor if0_out_ep;
	struct usb_ep_descriptor if0_in_ep;
	struct usb_ep_descriptor if0_hs_out_ep;
	struct usb_ep_descriptor if0_hs_in_ep;
	struct usb_desc_header nil_desc;
};

struct aa_usb_data {
	struct aa_usb_desc *const desc;
	const struct usb_desc_header **const fs_desc;
	const struct usb_desc_header **const hs_desc;
};

static struct aa_usb_desc aa_usb_desc = {
	.if0 = {
		.bLength = sizeof(struct usb_if_descriptor),
		.bDescriptorType = USB_DESC_INTERFACE,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 2,
		.bInterfaceClass = USB_BCC_VENDOR,
		.bInterfaceSubClass = 0xFF,
		.bInterfaceProtocol = 0,
		.iInterface = 0,
	},
	.if0_out_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = 0x01,
		.bmAttributes = USB_EP_TYPE_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(64U),
		.bInterval = 0x00,
	},
	.if0_in_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = 0x81,
		.bmAttributes = USB_EP_TYPE_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(64U),
		.bInterval = 0x00,
	},
	.if0_hs_out_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = 0x01,
		.bmAttributes = USB_EP_TYPE_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(512U),
		.bInterval = 0x00,
	},
	.if0_hs_in_ep = {
		.bLength = sizeof(struct usb_ep_descriptor),
		.bDescriptorType = USB_DESC_ENDPOINT,
		.bEndpointAddress = 0x81,
		.bmAttributes = USB_EP_TYPE_BULK,
		.wMaxPacketSize = sys_cpu_to_le16(512U),
		.bInterval = 0x00,
	},
	.nil_desc = {
		.bLength = 0,
		.bDescriptorType = 0,
	},
};

static const struct usb_desc_header *aa_usb_fs_desc[] = {
	(struct usb_desc_header *)&aa_usb_desc.if0,
	(struct usb_desc_header *)&aa_usb_desc.if0_out_ep,
	(struct usb_desc_header *)&aa_usb_desc.if0_in_ep,
	(struct usb_desc_header *)&aa_usb_desc.nil_desc,
};

static const struct usb_desc_header *aa_usb_hs_desc[] = {
	(struct usb_desc_header *)&aa_usb_desc.if0,
	(struct usb_desc_header *)&aa_usb_desc.if0_hs_out_ep,
	(struct usb_desc_header *)&aa_usb_desc.if0_hs_in_ep,
	(struct usb_desc_header *)&aa_usb_desc.nil_desc,
};

static struct aa_usb_data aa_usb_data = {
	.desc = &aa_usb_desc,
	.fs_desc = aa_usb_fs_desc,
	.hs_desc = aa_usb_hs_desc,
};

static bool is_high_speed(struct usbd_class_data *const c_data)
{
	struct usbd_context *uds_ctx = usbd_class_get_ctx(c_data);

	return USBD_SUPPORTS_HIGH_SPEED && usbd_bus_speed(uds_ctx) == USBD_SPEED_HS;
}

static uint8_t bulk_out_ep(struct usbd_class_data *const c_data)
{
	struct aa_usb_data *data = usbd_class_get_private(c_data);

	return is_high_speed(c_data) ? data->desc->if0_hs_out_ep.bEndpointAddress
				     : data->desc->if0_out_ep.bEndpointAddress;
}

static uint8_t bulk_in_ep(struct usbd_class_data *const c_data)
{
	struct aa_usb_data *data = usbd_class_get_private(c_data);

	return is_high_speed(c_data) ? data->desc->if0_hs_in_ep.bEndpointAddress
				     : data->desc->if0_in_ep.bEndpointAddress;
}

static uint16_t bulk_mps(struct usbd_class_data *const c_data)
{
	return is_high_speed(c_data) ? 512U : 64U;
}

static int submit_out(struct usbd_class_data *const c_data, struct net_buf *buf)
{
	struct udc_buf_info *bi = udc_get_buf_info(buf);
	int ret;

	net_buf_reset(buf);
	memset(bi, 0, sizeof(*bi));
	bi->ep = bulk_out_ep(c_data);

	ret = usbd_ep_enqueue(c_data, buf);
	if (ret != 0) {
		LOG_ERR("Failed to queue OUT transfer (%d)", ret);
		net_buf_unref(buf);
	}

	return ret;
}

/* Push received bytes into the pipe; returns true when everything was taken */
static bool push_rx(struct net_buf *buf)
{
	while (buf->len > 0U) {
		int n = k_pipe_write(&aa_rx_pipe, buf->data, buf->len, K_NO_WAIT);

		if (n <= 0) {
			return false;
		}
		net_buf_pull(buf, (size_t)n);
	}

	return true;
}

/* Called by the reader after draining the pipe to resume a parked OUT buffer */
static void rx_kick(void)
{
	struct net_buf *buf;
	k_spinlock_key_t key;

	key = k_spin_lock(&pending_lock);
	buf = pending_out;
	pending_out = NULL;
	k_spin_unlock(&pending_lock, key);

	if (buf == NULL) {
		return;
	}

	if (!atomic_get(&link_up)) {
		net_buf_unref(buf);
		return;
	}

	if (push_rx(buf)) {
		(void)submit_out(aa_c_data, buf);
		return;
	}

	key = k_spin_lock(&pending_lock);
	pending_out = buf;
	k_spin_unlock(&pending_lock, key);
}

static int aa_usb_request(struct usbd_class_data *const c_data, struct net_buf *buf, int err)
{
	struct udc_buf_info *bi = udc_get_buf_info(buf);

	if (bi->ep == bulk_in_ep(c_data)) {
		net_buf_unref(buf);
		return 0;
	}

	if (err != 0 || !atomic_get(&link_up)) {
		net_buf_unref(buf);
		return 0;
	}

	if (push_rx(buf)) {
		return submit_out(c_data, buf);
	}

	/* The pipe is full: park the buffer, the host is NAKed until the reader catches up */
	k_spinlock_key_t key = k_spin_lock(&pending_lock);

	pending_out = buf;
	k_spin_unlock(&pending_lock, key);

	return 0;
}

static void *aa_usb_get_desc(struct usbd_class_data *const c_data, const enum usbd_speed speed)
{
	struct aa_usb_data *data = usbd_class_get_private(c_data);

	return (speed == USBD_SPEED_HS) ? data->hs_desc : data->fs_desc;
}

static void aa_usb_enable(struct usbd_class_data *const c_data)
{
	LOG_INF("Accessory interface enabled (%s)", is_high_speed(c_data) ? "high speed"
									    : "full speed");

	atomic_set(&link_up, 1);

	for (int i = 0; i < 2; i++) {
		struct net_buf *buf = net_buf_alloc(&aa_out_pool, K_NO_WAIT);

		if (buf == NULL) {
			LOG_ERR("Out of OUT buffers");
			break;
		}
		(void)submit_out(c_data, buf);
	}

	k_sem_give(&link_sem);
}

static void aa_usb_disable(struct usbd_class_data *const c_data)
{
	struct net_buf *buf;
	k_spinlock_key_t key;

	ARG_UNUSED(c_data);

	LOG_INF("Accessory interface disabled");
	atomic_set(&link_up, 0);

	key = k_spin_lock(&pending_lock);
	buf = pending_out;
	pending_out = NULL;
	k_spin_unlock(&pending_lock, key);
	if (buf != NULL) {
		net_buf_unref(buf);
	}

	k_pipe_reset(&aa_rx_pipe);
}

static int aa_usb_init(struct usbd_class_data *const c_data)
{
	aa_c_data = c_data;

	return 0;
}

static struct usbd_class_api aa_usb_api = {
	.request = aa_usb_request,
	.get_desc = aa_usb_get_desc,
	.enable = aa_usb_enable,
	.disable = aa_usb_disable,
	.init = aa_usb_init,
};

USBD_DEFINE_CLASS(aa_usb, &aa_usb_api, &aa_usb_data, NULL);

static void usbd_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *const msg)
{
	LOG_DBG("USB message: %s", usbd_msg_type_string(msg->type));

	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			(void)usbd_enable(ctx);
		} else if (msg->type == USBD_MSG_VBUS_REMOVED) {
			(void)usbd_disable(ctx);
		}
	}
}

static int usb_open(void)
{
	int ret;

	if (CONFIG_SAMPLE_AA_USB_START_DELAY_MS > 0) {
		LOG_INF("Enabling USB in %u ms", CONFIG_SAMPLE_AA_USB_START_DELAY_MS);
		k_sleep(K_MSEC(CONFIG_SAMPLE_AA_USB_START_DELAY_MS));
	}

	usbd_ctx = sample_usbd_setup_device(usbd_msg_cb);
	if (usbd_ctx == NULL) {
		LOG_ERR("Failed to set up the USB device");
		return -ENODEV;
	}

	ret = aa_aoa_register(usbd_ctx);
	if (ret != 0) {
		return ret;
	}

	if (IS_ENABLED(CONFIG_SAMPLE_AA_USB_ACCESSORY_AT_BOOT)) {
		(void)usbd_device_set_vid(usbd_ctx, CONFIG_SAMPLE_AA_AOA_VID);
		(void)usbd_device_set_pid(usbd_ctx, CONFIG_SAMPLE_AA_AOA_PID);
	}

	ret = usbd_init(usbd_ctx);
	if (ret != 0) {
		LOG_ERR("Failed to initialize the USB device (%d)", ret);
		return ret;
	}

	if (usbd_can_detect_vbus(usbd_ctx)) {
		return 0;
	}

	ret = usbd_enable(usbd_ctx);
	if (ret != 0) {
		LOG_ERR("Failed to enable the USB device (%d)", ret);
	}

	return ret;
}

static int usb_wait_link(k_timeout_t timeout)
{
	if (atomic_get(&link_up)) {
		return 0;
	}

	if (k_sem_take(&link_sem, timeout) != 0) {
		return -ETIMEDOUT;
	}

	return atomic_get(&link_up) ? 0 : -ETIMEDOUT;
}

static int usb_read(uint8_t *buf, size_t len, k_timeout_t timeout)
{
	int n;

	if (!atomic_get(&link_up)) {
		return -ENOTCONN;
	}

	n = k_pipe_read(&aa_rx_pipe, buf, len, timeout);
	if (n > 0) {
		rx_kick();
		return n;
	}

	if (n == -EAGAIN) {
		return -ETIMEDOUT;
	}

	return -ENOTCONN;
}

static int usb_write(const uint8_t *buf, size_t len)
{
	size_t off = 0;

	if (!atomic_get(&link_up) || aa_c_data == NULL) {
		return -ENOTCONN;
	}

	while (off < len) {
		size_t n = MIN((size_t)IN_XFER_SIZE, len - off);
		struct net_buf *nb;
		struct udc_buf_info *bi;
		int ret;

		nb = net_buf_alloc(&aa_in_pool, K_MSEC(CONFIG_SAMPLE_AA_USB_WRITE_TIMEOUT_MS));
		if (nb == NULL) {
			return atomic_get(&link_up) ? -ETIMEDOUT : -ENOTCONN;
		}

		net_buf_add_mem(nb, buf + off, n);
		bi = udc_get_buf_info(nb);
		memset(bi, 0, sizeof(*bi));
		bi->ep = bulk_in_ep(aa_c_data);
		off += n;

		if (off == len && (n % bulk_mps(aa_c_data)) == 0U) {
			udc_ep_buf_set_zlp(nb);
		}

		ret = usbd_ep_enqueue(aa_c_data, nb);
		if (ret != 0) {
			net_buf_unref(nb);
			return atomic_get(&link_up) ? -EIO : -ENOTCONN;
		}
	}

	return 0;
}

static void usb_close(void)
{
	if (usbd_ctx == NULL) {
		return;
	}

	/* Detach so that the head unit notices the end of the session */
	(void)usbd_disable(usbd_ctx);
	k_sleep(K_MSEC(500));
	(void)usbd_enable(usbd_ctx);
}

static bool usb_is_up(void)
{
	return atomic_get(&link_up) != 0;
}

static const struct aa_transport usb_transport = {
	.open = usb_open,
	.wait_link = usb_wait_link,
	.read = usb_read,
	.write = usb_write,
	.close = usb_close,
	.is_up = usb_is_up,
	.name = "usb",
};

const struct aa_transport *aa_transport_get(void)
{
	return &usb_transport;
}
