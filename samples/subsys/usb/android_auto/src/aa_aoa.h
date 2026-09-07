/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_AOA_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_AOA_H_

#include <zephyr/usb/usbd.h>

/**
 * @brief Register the Android Open Accessory vendor requests.
 *
 * Must be called before usbd_init().
 *
 * @retval 0 Success.
 * @return Negative errno from the USB device stack.
 */
int aa_aoa_register(struct usbd_context *uds_ctx);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_AOA_H_ */
