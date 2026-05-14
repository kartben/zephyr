/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/meshtastic/nodedb.h>

typedef size_t (*meshtastic_test_nodedb_count_fn)(void);
typedef int (*meshtastic_test_nodedb_get_fn)(uint32_t node_num,
					     struct meshtastic_nodedb_node *out);
typedef int (*meshtastic_test_nodedb_get_by_index_fn)(size_t index,
						      struct meshtastic_nodedb_node *out);

meshtastic_test_nodedb_count_fn meshtastic_test_header_nodedb_count_only(void)
{
	return meshtastic_nodedb_count;
}

meshtastic_test_nodedb_get_fn meshtastic_test_header_nodedb_get_only(void)
{
	return meshtastic_nodedb_get;
}

meshtastic_test_nodedb_get_by_index_fn meshtastic_test_header_nodedb_get_by_index_only(void)
{
	return meshtastic_nodedb_get_by_index;
}
