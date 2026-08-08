/*
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2018,2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief USB HID Class device API header
 * @ingroup usb_hid_class
 */

#ifndef ZEPHYR_INCLUDE_USB_HID_CLASS_DEVICE_H_
#define ZEPHYR_INCLUDE_USB_HID_CLASS_DEVICE_H_

#include <zephyr/usb/class/hid.h>
#include <zephyr/usb/usb_ch9.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief usb_hid.h API
 * @defgroup usb_hid_class USB HID class API
 * @ingroup usb
 * @since 1.11
 * @version 1.0.0
 * @{
 */

/**
 * @defgroup usb_hid_device_api HID class USB specific definitions
 * @{
 */

/**
 * @brief Callback function signature for HID Get Report and Set Report requests
 *
 * For host to device direction (Set Report), @p len and @p data contain the
 * length and the pointer of the received report. For device to host direction
 * (Get Report), the callback must update @p len and @p data with the length
 * and the address of the buffer to be transmitted.
 *
 * @param[in]     dev   Pointer to USB HID device
 * @param[in]     setup Pointer to Control Setup packet of the request
 * @param[in,out] len   Pointer to the length of the data buffer
 * @param[in,out] data  Pointer to the data buffer
 *
 * @return 0 on success, negative errno code on fail.
 */
typedef int (*hid_cb_t)(const struct device *dev,
			struct usb_setup_packet *setup, int32_t *len,
			uint8_t **data);

/**
 * @brief Callback function signature for HID interrupt endpoint events
 *
 * @param[in] dev Pointer to USB HID device
 */
typedef void (*hid_int_ready_callback)(const struct device *dev);

/**
 * @brief Callback function signature for HID protocol change notification
 *
 * @param[in] dev      Pointer to USB HID device
 * @param[in] protocol New protocol, HID_PROTOCOL_BOOT or HID_PROTOCOL_REPORT
 */
typedef void (*hid_protocol_cb_t)(const struct device *dev, uint8_t protocol);

/**
 * @brief Callback function signature for idle rate expiry notification
 *
 * @param[in] dev       Pointer to USB HID device
 * @param[in] report_id Report ID for which the idle rate elapsed
 */
typedef void (*hid_idle_cb_t)(const struct device *dev, uint16_t report_id);

/**
 * @brief USB HID device interface
 */
struct hid_ops {
	/** Callback invoked to handle the HID Get Report request */
	hid_cb_t get_report;
	/** Callback invoked to handle the HID Set Report request */
	hid_cb_t set_report;
	/** Callback invoked when the host changed the protocol */
	hid_protocol_cb_t protocol_change;
	/** Callback invoked when the idle rate set by the host elapsed */
	hid_idle_cb_t on_idle;
	/**
	 * Optional callback that is called when the current interrupt IN
	 * transfer has completed.  This can be used to wait for the
	 * endpoint to go idle or to trigger the next transfer.
	 */
	hid_int_ready_callback int_in_ready;
	/**
	 * Optional callback that is called when a transfer on the
	 * interrupt OUT endpoint has completed and data is ready to
	 * be read.
	 */
	hid_int_ready_callback int_out_ready;
};

/**
 * @brief Register HID device
 *
 * @deprecated Use @ref usbd_hid_device instead.
 *
 * @param[in]  dev          Pointer to USB HID device
 * @param[in]  desc         Pointer to HID report descriptor
 * @param[in]  size         Size of HID report descriptor
 * @param[in]  op           Pointer to USB HID device interrupt struct
 */
__deprecated void usb_hid_register_device(const struct device *dev,
			     const uint8_t *desc,
			     size_t size,
			     const struct hid_ops *op);

/**
 * @brief Write to USB HID interrupt endpoint buffer
 *
 * @deprecated Use @ref usbd_hid_device instead.
 *
 * @param[in]  dev          Pointer to USB HID device
 * @param[in]  data         Pointer to data buffer
 * @param[in]  data_len     Length of data to copy
 * @param[out] bytes_ret    Bytes written to the EP buffer.
 *
 * @return 0 on success, negative errno code on fail.
 */
__deprecated int hid_int_ep_write(const struct device *dev,
		     const uint8_t *data,
		     uint32_t data_len,
		     uint32_t *bytes_ret);

/**
 * @brief Read from USB HID interrupt endpoint buffer
 *
 * @deprecated Use @ref usbd_hid_device instead.
 *
 * @param[in]  dev          Pointer to USB HID device
 * @param[in]  data         Pointer to data buffer
 * @param[in]  max_data_len Max length of data to copy
 * @param[out] ret_bytes    Number of bytes to copy.  If data is NULL and
 *                          max_data_len is 0 the number of bytes
 *                          available in the buffer will be returned.
 *
 * @return 0 on success, negative errno code on fail.
 */
__deprecated int hid_int_ep_read(const struct device *dev,
		    uint8_t *data,
		    uint32_t max_data_len,
		    uint32_t *ret_bytes);

/**
 * @brief Set USB HID class Protocol Code
 *
 * @deprecated Use @ref usbd_hid_device instead.
 *
 * @details Should be called before usb_hid_init().
 *
 * @param[in]  dev          Pointer to USB HID device
 * @param[in]  proto_code   Protocol Code to be used for bInterfaceProtocol
 *
 * @return 0 on success, negative errno code on fail.
 */
__deprecated int usb_hid_set_proto_code(const struct device *dev, uint8_t proto_code);

/**
 * @brief Initialize USB HID class support
 *
 * @deprecated Use @ref usbd_hid_device instead.
 *
 * @param[in]  dev          Pointer to USB HID device
 *
 * @return 0 on success, negative errno code on fail.
 */
__deprecated int usb_hid_init(const struct device *dev);

/**
 * @}
 */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_USB_HID_CLASS_DEVICE_H_ */
