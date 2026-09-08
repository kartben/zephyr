/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_transport.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/usbh.h>

#include <usbh_ch9.h>
#include <usbh_class.h>
#include <usbh_desc.h>
#include <usbh_device.h>

LOG_MODULE_REGISTER(aa_usbh, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * USB host transport: the head unit is the USB host and the phone is the
 * device. A freshly attached phone enumerates with its own identity, so it is
 * first switched into Android Open Accessory mode with the vendor requests
 * below. It then re-enumerates with Google's accessory ids and a vendor
 * interface holding the bulk endpoints that carry the projection protocol.
 *
 * Two classes are registered: one matches every device to perform the switch,
 * the other matches the accessory interface and owns the endpoints.
 */

#define AOA_REQ_GET_PROTOCOL 51U
#define AOA_REQ_SEND_STRING  52U
#define AOA_REQ_START        53U

#define AOA_VID          0x18D1U
#define AOA_PID_ACCESSORY 0x2D00U
#define AOA_PID_ACCESSORY_ADB 0x2D01U

#define AOA_REQTYPE_IN                                                                             \
	((USB_REQTYPE_DIR_TO_HOST << 7) | (USB_REQTYPE_TYPE_VENDOR << 5) |                         \
	 USB_REQTYPE_RECIPIENT_DEVICE)
#define AOA_REQTYPE_OUT                                                                            \
	((USB_REQTYPE_DIR_TO_DEVICE << 7) | (USB_REQTYPE_TYPE_VENDOR << 5) |                       \
	 USB_REQTYPE_RECIPIENT_DEVICE)

#define IN_XFER_SIZE  CONFIG_SAMPLE_AA_HU_USBH_IN_XFER_SIZE
#define IN_ERROR_LIMIT 16U
#define OUT_XFER_SIZE CONFIG_SAMPLE_AA_HU_USBH_OUT_XFER_SIZE

/*
 * The identity presented to the phone. "Android Auto" as the model is what
 * makes the phone start projection instead of a plain accessory session.
 */
static const char *const aoa_strings[] = {
	CONFIG_SAMPLE_AA_HU_AOA_MANUFACTURER, CONFIG_SAMPLE_AA_HU_AOA_MODEL,
	CONFIG_SAMPLE_AA_HU_AOA_DESCRIPTION,  CONFIG_SAMPLE_AA_HU_AOA_VERSION,
	CONFIG_SAMPLE_AA_HU_AOA_URI,          CONFIG_SAMPLE_AA_HU_AOA_SERIAL,
};

USBH_CONTROLLER_DEFINE(aa_hu_uhs, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));

K_PIPE_DEFINE(aa_rx_pipe, CONFIG_SAMPLE_AA_HU_USBH_RX_PIPE_SIZE, 4);

static K_SEM_DEFINE(link_sem, 0, 1);
static atomic_t link_up;

/* The claimed accessory interface. Only touched from the host stack threads. */
static struct {
	struct usb_device *udev;
	uint8_t ep_in;
	uint8_t ep_out;
	uint16_t mps_in;
	struct uhc_transfer *in_xfer;
} acc;

/* Consecutive failed IN transfers, reset by a good one */
static uint8_t in_errors;

/* IN data that did not fit the pipe, resumed by the reader */
static struct net_buf *pending_in;
static struct k_spinlock pending_lock;

/* Serializes writes and tracks the in-flight OUT transfer */
static K_MUTEX_DEFINE(write_mutex);
static K_SEM_DEFINE(write_sem, 0, 1);
static struct k_spinlock write_lock;
static bool write_done;
static bool write_abandoned;
static int write_err;

static int arm_in_xfer(void);

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

/*
 * Completion of the bulk IN transfer. A bulk IN finishes on a short packet or
 * a full buffer, so this is simply the next slice of the byte stream.
 */
static int bulk_in_cb(struct usb_device *const udev, struct uhc_transfer *const xfer)
{
	struct net_buf *buf = xfer->buf;

	xfer->buf = NULL;

	if (buf != NULL && (xfer->err != 0 || !atomic_get(&link_up))) {
		usbh_xfer_buf_free(udev, buf);
		buf = NULL;
	}

	if (xfer->err != 0) {
		/*
		 * The controller reports the occasional transaction error on
		 * an idle endpoint. Poll again instead of dropping the link,
		 * but give up if they keep coming.
		 */
		if (!atomic_get(&link_up)) {
			return 0;
		}

		if (++in_errors == IN_ERROR_LIMIT) {
			LOG_WRN("Bulk IN keeps failing (%d), still polling", xfer->err);
		}

		return arm_in_xfer();
	}

	if (buf == NULL) {
		return 0;
	}

	in_errors = 0;

	if (push_rx(buf)) {
		usbh_xfer_buf_free(udev, buf);
		return arm_in_xfer();
	}

	/*
	 * The pipe is full: park the buffer and stop polling. The phone is
	 * NAKed until the reader drains the pipe and resumes the transfer.
	 */
	K_SPINLOCK(&pending_lock) {
		pending_in = buf;
	}

	return 0;
}

/* Queue the standing IN transfer, allocating it on first use */
static int arm_in_xfer(void)
{
	struct net_buf *buf;
	int ret;

	if (acc.udev == NULL) {
		return -ENOTCONN;
	}

	if (acc.in_xfer == NULL) {
		acc.in_xfer = usbh_xfer_alloc(acc.udev, acc.ep_in, bulk_in_cb, NULL);
		if (acc.in_xfer == NULL) {
			LOG_ERR("Out of transfers");
			return -ENOMEM;
		}
	}

	buf = usbh_xfer_buf_alloc(acc.udev, IN_XFER_SIZE);
	if (buf == NULL) {
		LOG_ERR("Out of IN buffers");
		return -ENOMEM;
	}

	acc.in_xfer->buf = buf;

	ret = usbh_xfer_enqueue(acc.udev, acc.in_xfer);
	if (ret != 0) {
		LOG_ERR("Failed to queue the IN transfer (%d)", ret);
		acc.in_xfer->buf = NULL;
		usbh_xfer_buf_free(acc.udev, buf);
	}

	return ret;
}

/* Called by the reader after draining the pipe to resume a parked buffer */
static void rx_kick(void)
{
	struct net_buf *buf = NULL;

	K_SPINLOCK(&pending_lock) {
		buf = pending_in;
		pending_in = NULL;
	}

	if (buf == NULL) {
		return;
	}

	if (!atomic_get(&link_up) || acc.udev == NULL) {
		return;
	}

	if (push_rx(buf)) {
		usbh_xfer_buf_free(acc.udev, buf);
		(void)arm_in_xfer();
		return;
	}

	K_SPINLOCK(&pending_lock) {
		pending_in = buf;
	}
}

static int bulk_out_cb(struct usb_device *const udev, struct uhc_transfer *const xfer)
{
	bool abandoned;

	if (xfer->buf != NULL) {
		usbh_xfer_buf_free(udev, xfer->buf);
		xfer->buf = NULL;
	}

	K_SPINLOCK(&write_lock) {
		write_err = xfer->err;
		write_done = true;
		abandoned = write_abandoned;
	}

	if (abandoned) {
		/* The writer gave up waiting, clean up on its behalf */
		usbh_xfer_free(udev, xfer);
		return 0;
	}

	k_sem_give(&write_sem);

	return 0;
}

/*
 * Android Open Accessory switch. Runs on the host stack's bus thread, where
 * the synchronous control requests below are allowed to block.
 */
static int aoa_switch_device(struct usb_device *const udev)
{
	struct net_buf *buf;
	uint16_t version;
	int ret;

	buf = usbh_xfer_buf_alloc(udev, sizeof(uint16_t));
	if (buf == NULL) {
		return -ENOMEM;
	}

	ret = usbh_req_setup(udev, AOA_REQTYPE_IN, AOA_REQ_GET_PROTOCOL, 0, 0, sizeof(uint16_t),
			     buf);
	if (ret != 0 || buf->len < sizeof(uint16_t)) {
		LOG_DBG("Not an Android device (%d)", ret);
		usbh_xfer_buf_free(udev, buf);
		return -ENOTSUP;
	}

	version = sys_get_le16(buf->data);
	usbh_xfer_buf_free(udev, buf);

	if (version == 0U) {
		LOG_WRN("Device does not support the accessory protocol");
		return -ENOTSUP;
	}

	LOG_INF("Android device, accessory protocol version %u", version);

	for (uint16_t i = 0; i < ARRAY_SIZE(aoa_strings); i++) {
		const char *str = aoa_strings[i];
		uint16_t len = (uint16_t)strlen(str) + 1U;

		buf = usbh_xfer_buf_alloc(udev, len);
		if (buf == NULL) {
			return -ENOMEM;
		}

		net_buf_add_mem(buf, str, len);
		ret = usbh_req_setup(udev, AOA_REQTYPE_OUT, AOA_REQ_SEND_STRING, 0, i, len, buf);
		usbh_xfer_buf_free(udev, buf);

		if (ret != 0) {
			LOG_ERR("Accessory string %u rejected (%d)", i, ret);
			return ret;
		}
	}

	ret = usbh_req_setup(udev, AOA_REQTYPE_OUT, AOA_REQ_START, 0, 0, 0, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to start accessory mode (%d)", ret);
		return ret;
	}

	LOG_INF("Accessory mode requested, waiting for the phone to re-enumerate");

	return 0;
}

static int aoa_switch_init(struct usbh_class_data *const c_data)
{
	ARG_UNUSED(c_data);

	return 0;
}

static int aoa_switch_probe(struct usbh_class_data *const c_data, struct usb_device *const udev,
			    const uint8_t iface)
{
	ARG_UNUSED(c_data);

	/* Only look at the device as a whole, not at each of its functions */
	if (iface != USBH_CLASS_IFNUM_DEVICE) {
		return -ENOTSUP;
	}

	if (udev->dev_desc.idVendor == AOA_VID &&
	    (udev->dev_desc.idProduct == AOA_PID_ACCESSORY ||
	     udev->dev_desc.idProduct == AOA_PID_ACCESSORY_ADB)) {
		/* Already an accessory, leave it to the accessory class */
		return -ENOTSUP;
	}

	LOG_INF("New device %04x:%04x", udev->dev_desc.idVendor, udev->dev_desc.idProduct);

	(void)aoa_switch_device(udev);

	/*
	 * Never bind: the phone detaches and comes back as an accessory, and
	 * this instance has to stay free to switch the next device.
	 */
	return -ENOTSUP;
}

static int aoa_switch_removed(struct usbh_class_data *const c_data)
{
	ARG_UNUSED(c_data);

	return 0;
}

static struct usbh_class_api aoa_switch_api = {
	.init = aoa_switch_init,
	.probe = aoa_switch_probe,
	.removed = aoa_switch_removed,
};

USBH_DEFINE_CLASS(aa_aoa_switch, &aoa_switch_api, NULL, NULL);

/* Accessory interface: one bulk IN and one bulk OUT endpoint */
static int accessory_find_endpoints(struct usb_device *const udev, const uint8_t iface)
{
	const struct usb_desc_header *desc = usbh_desc_get_iface(udev, iface);

	if (desc == NULL || desc->bDescriptorType != USB_DESC_INTERFACE) {
		return -ENOENT;
	}

	acc.ep_in = 0U;
	acc.ep_out = 0U;

	for (desc = usbh_desc_get_next(desc); desc != NULL; desc = usbh_desc_get_next(desc)) {
		const struct usb_ep_descriptor *ep = (const struct usb_ep_descriptor *)desc;

		if (desc->bDescriptorType == USB_DESC_INTERFACE) {
			break;
		}

		if (desc->bDescriptorType != USB_DESC_ENDPOINT) {
			continue;
		}

		if ((ep->bmAttributes & USB_EP_TRANSFER_TYPE_MASK) != USB_EP_TYPE_BULK) {
			continue;
		}

		if (USB_EP_DIR_IS_IN(ep->bEndpointAddress)) {
			acc.ep_in = ep->bEndpointAddress;
			acc.mps_in = ep->wMaxPacketSize;
		} else {
			acc.ep_out = ep->bEndpointAddress;
		}
	}

	if (acc.ep_in == 0U || acc.ep_out == 0U) {
		return -ENOENT;
	}

	return 0;
}

static int accessory_init(struct usbh_class_data *const c_data)
{
	ARG_UNUSED(c_data);

	return 0;
}

static int accessory_probe(struct usbh_class_data *const c_data, struct usb_device *const udev,
			   const uint8_t iface)
{
	int ret;

	ARG_UNUSED(c_data);

	if (iface == USBH_CLASS_IFNUM_DEVICE) {
		return -ENOTSUP;
	}

	if (udev->dev_desc.idVendor != AOA_VID ||
	    (udev->dev_desc.idProduct != AOA_PID_ACCESSORY &&
	     udev->dev_desc.idProduct != AOA_PID_ACCESSORY_ADB)) {
		return -ENOTSUP;
	}

	ret = accessory_find_endpoints(udev, iface);
	if (ret != 0) {
		LOG_WRN("Interface %u has no bulk endpoint pair", iface);
		return -ENOTSUP;
	}

	acc.udev = udev;
	acc.in_xfer = NULL;
	in_errors = 0;

	LOG_INF("Accessory interface %u, bulk in 0x%02x out 0x%02x, %u byte packets", iface,
		acc.ep_in, acc.ep_out, acc.mps_in);

	atomic_set(&link_up, 1);

	ret = arm_in_xfer();
	if (ret != 0) {
		atomic_set(&link_up, 0);
		acc.udev = NULL;
		return -ENOTSUP;
	}

	k_sem_give(&link_sem);

	return 0;
}

static int accessory_removed(struct usbh_class_data *const c_data)
{
	ARG_UNUSED(c_data);

	LOG_INF("Accessory detached");

	atomic_set(&link_up, 0);

	/*
	 * The device object is freed as soon as this returns and the host
	 * controller does not hand back transfers that were still queued, so
	 * drop every reference to them here and start over on the next attach.
	 */
	K_SPINLOCK(&pending_lock) {
		pending_in = NULL;
	}

	acc.udev = NULL;
	acc.in_xfer = NULL;

	k_pipe_reset(&aa_rx_pipe);
	k_sem_give(&write_sem);

	return 0;
}

static struct usbh_class_api accessory_api = {
	.init = accessory_init,
	.probe = accessory_probe,
	.removed = accessory_removed,
};

static const struct usbh_class_filter accessory_filters[] = {
	{
		.class = USB_BCC_VENDOR,
		.sub = 0xFFU,
		.proto = 0x00U,
		.flags = USBH_CLASS_MATCH_CODE_TRIPLE,
	},
	{0},
};

USBH_DEFINE_CLASS(aa_accessory, &accessory_api, NULL, accessory_filters);

static int usbh_open(void)
{
	int ret;

	ret = usbh_init(&aa_hu_uhs);
	if (ret != 0) {
		LOG_ERR("Failed to initialize USB host support (%d)", ret);
		return ret;
	}

	ret = usbh_enable(&aa_hu_uhs);
	if (ret != 0) {
		LOG_ERR("Failed to enable USB host support (%d)", ret);
		return ret;
	}

	LOG_INF("USB host started, waiting for a phone");

	return 0;
}

static int usbh_wait_link(k_timeout_t timeout)
{
	if (atomic_get(&link_up)) {
		return 0;
	}

	if (k_sem_take(&link_sem, timeout) != 0) {
		return -ETIMEDOUT;
	}

	return atomic_get(&link_up) ? 0 : -ETIMEDOUT;
}

static int usbh_read(uint8_t *buf, size_t len, k_timeout_t timeout)
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

static int usbh_write(const uint8_t *buf, size_t len)
{
	size_t off = 0;
	int ret = 0;

	if (!atomic_get(&link_up)) {
		return -ENOTCONN;
	}

	k_mutex_lock(&write_mutex, K_FOREVER);

	while (off < len) {
		size_t n = MIN((size_t)OUT_XFER_SIZE, len - off);
		struct uhc_transfer *xfer;
		struct net_buf *nb;
		bool taken;

		if (!atomic_get(&link_up) || acc.udev == NULL) {
			ret = -ENOTCONN;
			break;
		}

		xfer = usbh_xfer_alloc(acc.udev, acc.ep_out, bulk_out_cb, NULL);
		if (xfer == NULL) {
			ret = -ENOMEM;
			break;
		}

		nb = usbh_xfer_buf_alloc(acc.udev, n);
		if (nb == NULL) {
			usbh_xfer_free(acc.udev, xfer);
			ret = -ENOMEM;
			break;
		}

		net_buf_add_mem(nb, buf + off, n);
		xfer->buf = nb;

		K_SPINLOCK(&write_lock) {
			write_done = false;
			write_abandoned = false;
			write_err = 0;
		}
		k_sem_reset(&write_sem);

		ret = usbh_xfer_enqueue(acc.udev, xfer);
		if (ret != 0) {
			xfer->buf = NULL;
			usbh_xfer_buf_free(acc.udev, nb);
			usbh_xfer_free(acc.udev, xfer);
			ret = -EIO;
			break;
		}

		if (k_sem_take(&write_sem, K_MSEC(CONFIG_SAMPLE_AA_HU_USBH_WRITE_TIMEOUT_MS)) !=
		    0) {
			/*
			 * A queued bulk transfer cannot be cancelled, so hand
			 * it to the completion callback and stop using it.
			 */
			K_SPINLOCK(&write_lock) {
				taken = write_done;
				write_abandoned = !taken;
			}

			if (taken && acc.udev != NULL) {
				usbh_xfer_free(acc.udev, xfer);
			}

			ret = atomic_get(&link_up) ? -ETIMEDOUT : -ENOTCONN;
			break;
		}

		/*
		 * Detaching wakes this up as well, and takes the device and
		 * everything allocated from it with it, so the transfer must
		 * only be given back while the phone is still there.
		 */
		if (!atomic_get(&link_up) || acc.udev == NULL) {
			ret = -ENOTCONN;
			break;
		}

		usbh_xfer_free(acc.udev, xfer);

		if (write_err != 0) {
			LOG_WRN("Bulk OUT failed (%d)", write_err);
			ret = -ENOTCONN;
			break;
		}

		off += n;
	}

	k_mutex_unlock(&write_mutex);

	return ret;
}

static void usbh_close(void)
{
	atomic_set(&link_up, 0);
	k_pipe_reset(&aa_rx_pipe);
}

static bool usbh_is_up(void)
{
	return atomic_get(&link_up) != 0;
}

static const struct aa_transport usbh_transport = {
	.open = usbh_open,
	.wait_link = usbh_wait_link,
	.read = usbh_read,
	.write = usbh_write,
	.close = usbh_close,
	.is_up = usbh_is_up,
	.name = "usb host",
};

const struct aa_transport *aa_transport_get(void)
{
	return &usbh_transport;
}
