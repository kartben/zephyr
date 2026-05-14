/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/meshtastic/nodeinfo.h>

typedef int (*meshtastic_test_send_node_info_fn)(uint32_t dest);

meshtastic_test_send_node_info_fn meshtastic_test_header_nodeinfo_only(void)
{
	return meshtastic_send_node_info;
}
