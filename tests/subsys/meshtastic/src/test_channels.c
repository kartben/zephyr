/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "meshtastic_channels.h"

#include <zephyr/meshtastic/meshtastic.h>

ZTEST(meshtastic_channels, test_longfast_hash)
{
	struct meshtastic_config cfg = {
		.psk = meshtastic_default_psk,
		.psk_len = sizeof(meshtastic_default_psk),
		.channel_name = MESHTASTIC_CHANNEL_LONGFAST,
		.frequency = MESHTASTIC_FREQ_US,
	};
	uint8_t h;

	zassert_ok(meshtastic_channels_init_from_config(&cfg));
	h = meshtastic_channels_get_hash(0);
	/* xor("LongFast") ^ xor(default_psk) — stable wire hash for default channel. */
	zassert_not_equal(h, 0U, "primary hash should be non-zero");
}

ZTEST(meshtastic_channels, test_shorthand_psk_expansion)
{
	meshtastic_Channel ch = meshtastic_Channel_init_zero;
	struct meshtastic_channel_key key;

	zassert_ok(meshtastic_channels_init_defaults());

	ch.index = 1;
	ch.role = meshtastic_Channel_Role_SECONDARY;
	ch.has_settings = true;
	strncpy(ch.settings.name, "Test", sizeof(ch.settings.name) - 1U);
	ch.settings.psk.bytes[0] = 1U;
	ch.settings.psk.size = 1U;

	zassert_ok(meshtastic_channels_set_slot(1, &ch));
	zassert_ok(meshtastic_channels_get_key(1, &key));
	zassert_equal(key.len, sizeof(meshtastic_default_psk), NULL);
	zassert_mem_equal(key.bytes, meshtastic_default_psk, key.len, NULL);
}

ZTEST(meshtastic_channels, test_secondary_inherits_primary_psk)
{
	meshtastic_Channel ch = meshtastic_Channel_init_zero;
	struct meshtastic_channel_key primary;
	struct meshtastic_channel_key secondary;

	zassert_ok(meshtastic_channels_init_defaults());

	ch.index = 2;
	ch.role = meshtastic_Channel_Role_SECONDARY;
	ch.has_settings = true;
	strncpy(ch.settings.name, "Inherited", sizeof(ch.settings.name) - 1U);
	ch.settings.psk.size = 0U;

	zassert_ok(meshtastic_channels_set_slot(2, &ch));
	zassert_ok(meshtastic_channels_get_key(0, &primary));
	zassert_ok(meshtastic_channels_get_key(2, &secondary));
	zassert_equal(primary.len, secondary.len, NULL);
	zassert_mem_equal(primary.bytes, secondary.bytes, primary.len, NULL);
}

ZTEST_SUITE(meshtastic_channels, NULL, NULL, NULL, NULL, NULL);
