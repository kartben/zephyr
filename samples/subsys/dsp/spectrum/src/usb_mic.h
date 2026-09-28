/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLE_SPECTRUM_USB_MIC_H_
#define SAMPLE_SPECTRUM_USB_MIC_H_

#include <stddef.h>
#include <stdint.h>

int usb_mic_init(void);
void usb_mic_feed(const int16_t *samples, size_t count);

#endif /* SAMPLE_SPECTRUM_USB_MIC_H_ */
