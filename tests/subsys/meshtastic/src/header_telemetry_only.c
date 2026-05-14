/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/meshtastic/telemetry.h>

typedef int (*meshtastic_test_send_metrics_fn)(uint32_t dest);

meshtastic_test_send_metrics_fn meshtastic_test_header_telemetry_only(void)
{
	return meshtastic_send_device_metrics;
}
