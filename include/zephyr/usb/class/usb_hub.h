/*
 * Copyright (c) 2022 Emerson Electric Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief USB Hub Class device API header
 * @ingroup usb
 */

#ifndef ZEPHYR_INCLUDE_USB_CLASS_USB_HUB_H_
#define ZEPHYR_INCLUDE_USB_CLASS_USB_HUB_H_

/* USB Hub Class Feature Selectors defined in spec. Table 11-17 */
#define USB_HCFS_C_HUB_LOCAL_POWER	0x00 /**< Hub local power source change */
#define USB_HCFS_C_HUB_OVER_CURRENT	0x01 /**< Hub over-current condition change */
#define USB_HCFS_PORT_CONNECTION	0x00 /**< Port current connect status */
#define USB_HCFS_PORT_ENABLE		0x01 /**< Port enabled/disabled state */
#define USB_HCFS_PORT_SUSPEND		0x02 /**< Port suspended state */
#define USB_HCFS_PORT_OVER_CURRENT	0x03 /**< Port over-current indicator */
#define USB_HCFS_PORT_RESET		0x04 /**< Port reset signaling */
#define USB_HCFS_PORT_POWER		0x08 /**< Port power state */
#define USB_HCFS_PORT_LOW_SPEED		0x09 /**< Low-speed device attached */
#define USB_HCFS_C_PORT_CONNECTION	0x10 /**< Port connect status change */
#define USB_HCFS_C_PORT_ENABLE		0x11 /**< Port enable/disable status change */
#define USB_HCFS_C_PORT_SUSPEND		0x12 /**< Port suspend status change */
#define USB_HCFS_C_PORT_OVER_CURRENT	0x13 /**< Port over-current indicator change */
#define USB_HCFS_C_PORT_RESET		0x14 /**< Port reset status change */
#define USB_HCFS_PORT_TEST		0x15 /**< Port test mode */
#define USB_HCFS_PORT_INDICATOR		0x16 /**< Port indicator control */

/* USB Hub Class Request Codes defined in spec. Table 11-16 */
#define USB_HCREQ_GET_STATUS		0x00 /**< Get Hub/Port Status request */
#define USB_HCREQ_CLEAR_FEATURE		0x01 /**< Clear Hub/Port Feature request */
#define USB_HCREQ_SET_FEATURE		0x03 /**< Set Hub/Port Feature request */
#define USB_HCREQ_GET_DESCRIPTOR	0x06 /**< Get Hub Descriptor request */
#define USB_HCREQ_SET_DESCRIPTOR	0x07 /**< Set Hub Descriptor request */
#define USB_HCREQ_CLEAR_TT_BUFFER	0x08 /**< Clear TT Buffer request */
#define USB_HCREQ_RESET_TT		0x09 /**< Reset TT request */
#define USB_HCREQ_GET_TT_STATE		0x0A /**< Get TT State request */
#define USB_HCREQ_STOP_TT		0x0B /**< Stop TT request */

#endif /* ZEPHYR_INCLUDE_USB_CLASS_USB_HUB_H_ */
