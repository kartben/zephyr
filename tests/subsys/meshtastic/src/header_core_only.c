/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/meshtastic/meshtastic.h>

uint32_t meshtastic_test_header_core_only(void)
{
	return MESHTASTIC_NODE_BROADCAST;
}
