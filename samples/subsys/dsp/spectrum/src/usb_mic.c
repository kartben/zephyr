/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <sample_usbd.h>

#include <zephyr/device.h>
#include <zephyr/drivers/usb/usb_buf.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/class/usbd_uac2.h>
#include <zephyr/usb/usbd.h>

#include "usb_mic.h"

#define MIC_TERMINAL UAC2_ENTITY_ID(DT_NODELABEL(mic_output))
#define FS_SAMPLES 16
#define HS_SAMPLES 2
#define MAX_SAMPLES (FS_SAMPLES + 1)
#define RING_BYTES 4096
#define TARGET_BYTES 1024

/* The producer is the audio capture thread; the USB SOF callback is the consumer. */
RING_BUF_DECLARE(pcm_ring, RING_BYTES);
K_MEM_SLAB_DEFINE_STATIC(packet_slab, ROUND_UP(MAX_SAMPLES * sizeof(int16_t),
						UDC_BUF_GRANULARITY), 16, UDC_BUF_ALIGN);
static atomic_t enabled;
static atomic_t microframes;

void usb_mic_feed(const int16_t *samples, size_t count)
{
	if (!atomic_get(&enabled)) {
		return;
	}
	/* Dropping new audio on overflow keeps this callback nonblocking. */
	(void)ring_buf_put(&pcm_ring, (const uint8_t *)samples, count * sizeof(*samples));
}

static void terminal_update(const struct device *dev, uint8_t terminal, bool active,
			    bool use_microframes, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (terminal == MIC_TERMINAL) {
		atomic_set(&microframes, use_microframes);
		atomic_set(&enabled, active);
		if (!active) {
			(void)ring_buf_consume(&pcm_ring, ring_buf_size_get(&pcm_ring));
		}
	}
}

static void buffer_release(const struct device *dev, uint8_t terminal, void *buf,
			   void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(terminal);
	ARG_UNUSED(user_data);

	k_mem_slab_free(&packet_slab, buf);
}

static void sof(const struct device *dev, void *user_data)
{
	void *packet;
	unsigned int nominal = atomic_get(&microframes) ? HS_SAMPLES : FS_SAMPLES;
	unsigned int queued = ring_buf_size_get(&pcm_ring);
	unsigned int count = nominal;
	unsigned int bytes;
	unsigned int read;

	ARG_UNUSED(user_data);
	if (!atomic_get(&enabled)) {
		return;
	}
	/* Non-SOF-synchronized UAC2 input endpoints allow one additional sample.
	 * Varying the packet length compensates for DMIC and USB clock drift.
	 */
	if (queued > TARGET_BYTES + 512) {
		count++;
	} else if (queued < TARGET_BYTES - 512 && count > 1) {
		count--;
	}
	bytes = count * sizeof(int16_t);
	if (k_mem_slab_alloc(&packet_slab, &packet, K_NO_WAIT) != 0) {
		return;
	}
	read = ring_buf_get(&pcm_ring, packet, bytes);
	if (read < bytes) {
		memset((uint8_t *)packet + read, 0, bytes - read);
	}
	if (usbd_uac2_send(dev, MIC_TERMINAL, packet, bytes) < 0) {
		k_mem_slab_free(&packet_slab, packet);
	}
}

int usb_mic_init(void)
{
	const struct device *mic = DEVICE_DT_GET(DT_NODELABEL(uac2_microphone));
	static struct uac2_ops ops = {
		.sof_cb = sof,
		.terminal_update_cb = terminal_update,
		.buf_release_cb = buffer_release,
	};
	struct usbd_context *usb;

	if (!device_is_ready(mic)) {
		return -ENODEV;
	}
	usbd_uac2_set_ops(mic, &ops, NULL);
	usb = sample_usbd_init_device(NULL);
	if (usb == NULL) {
		return -ENODEV;
	}
	return usbd_enable(usb);
}
