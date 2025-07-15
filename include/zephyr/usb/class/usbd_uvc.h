/*
 * Copyright (c) 2025 tinyVision.ai Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief USB Video Class (UVC) public header
 */

#ifndef ZEPHYR_INCLUDE_USB_CLASS_USBD_UVC_H
#define ZEPHYR_INCLUDE_USB_CLASS_USBD_UVC_H

#include <zephyr/device.h>

/**
 * @brief USB Video Class (UVC) device API
 * @defgroup usbd_uvc USB Video Class (UVC) device API
 * @ingroup usb
 * @since 4.2
 * @version 0.1.0
 * @see uvc: "Universal Serial Bus Device Class Definition for Video Devices"
 *      Document Release 1.5 (August 9, 2012)
 * @{
 */

/**
 * @brief Set the video device that a UVC instance will use for control requests.
 *
 * It will query its supported video controls and frame intervals and use this information to
 * generate the USB descriptors presented to the host. At runtime, it will forward all USB controls
 * from the host to this device.
 *
 * @note This function must be called before @ref usbd_enable.
 *
 * @param uvc_dev The UVC device
 * @param video_dev The video device that this UVC instance send controls requests to
 */
void uvc_set_video_dev(const struct device *uvc_dev, const struct device *video_dev);

/**
 * @brief Set the video format capabilities that a UVC instance will present to the host.
 *
 * This information will be used to generate USB descriptors.
 * The minimum and maximum values are considered as discrete values, as width and height ranges
 * are not supported by the UVC protocol, but only discrete values.
 *
 * The format selected by the host can then be queried through the @ref video_get_format API.
 *
 * @note This function must be called before @ref usbd_enable.
 *
 * @note The @p fmts memory pointed should remain valid for the entire lifetime of @p uvc_dev.
 *
 * @param uvc_dev The UVC device
 * @param fmt The video format to add
 * @return 0 on success, negative error code otherwise
 */
int uvc_add_format(const struct device *const dev, const struct video_format *const fmt);

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_USB_CLASS_USBD_UVC_H */
