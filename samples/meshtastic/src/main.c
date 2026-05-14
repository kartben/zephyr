/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Meshtastic sample application.
 *
 * Demonstrates basic usage of the Meshtastic subsystem:
 *  - Initialise the stack with the default LongFast channel.
 *  - Print every received text message to the console.
 *  - Broadcast "Hello from Zephyr!" every 30 seconds.
 *
 * The LoRa device is obtained from the DT alias "lora0".
 * The local node ID is taken from CONFIG_MESHTASTIC_SAMPLE_NODE_ID.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <zephyr/meshtastic/meshtastic.h>

LOG_MODULE_REGISTER(meshtastic_sample, LOG_LEVEL_INF);

/* Obtain LoRa device from the "lora0" devicetree alias. */
static const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));

/* Called from the Meshtastic receive thread for every decoded packet. */
static void on_recv(uint32_t from, uint32_t to,
		    uint8_t portnum,
		    const uint8_t *payload, size_t payload_len,
		    int16_t rssi, int8_t snr)
{
	if (portnum == MESHTASTIC_PORT_TEXT_MESSAGE) {
		/* Payload is raw UTF-8, not NUL-terminated — print with bound. */
		LOG_INF("MSG from 0x%08x: %.*s  (RSSI %d dBm, SNR %d)",
			from, (int)payload_len, (const char *)payload,
			(int)rssi, (int)snr);
	} else {
		LOG_INF("PKT from 0x%08x port=%u len=%zu RSSI=%d",
			from, (unsigned int)portnum, payload_len, (int)rssi);
	}
}

int main(void)
{
	struct meshtastic_config cfg = {
		.lora_dev = lora_dev,
		.node_id = CONFIG_MESHTASTIC_SAMPLE_NODE_ID,
		.psk = meshtastic_default_psk,
		.psk_len = sizeof(meshtastic_default_psk),
		.channel_name = MESHTASTIC_CHANNEL_LONGFAST,
		.frequency = MESHTASTIC_FREQ_EU,
		/* hop_limit and tx_power: 0 → use Kconfig defaults */
	};
	int ret;

	if (!device_is_ready(lora_dev)) {
		LOG_ERR("LoRa device not ready");
		return -ENODEV;
	}

	ret = meshtastic_init(&cfg);
	if (ret < 0) {
		LOG_ERR("meshtastic_init failed (%d)", ret);
		return ret;
	}

	meshtastic_set_recv_cb(on_recv);

	LOG_INF("Meshtastic sample started, node ID 0x%08x",
		meshtastic_get_node_id());

	/* Periodically broadcast a text message. */
	while (true) {
		ret = meshtastic_send_text(MESHTASTIC_NODE_BROADCAST,
					  "Hello from Zephyr!");
		if (ret < 0) {
			LOG_ERR("meshtastic_send_text failed (%d)", ret);
		} else {
			LOG_INF("Broadcast sent");
		}

		k_sleep(K_SECONDS(30));
	}

	return 0;
}
