/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/meshtastic/gnss.h>

typedef int (*meshtastic_test_send_position_fn)(uint32_t dest);

meshtastic_test_send_position_fn meshtastic_test_header_gnss_only(void)
{
	return meshtastic_send_position;
}
