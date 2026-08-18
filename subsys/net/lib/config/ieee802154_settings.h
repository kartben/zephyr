/* IEEE 802.15.4 settings header */

#ifndef ZEPHYR_SUBSYS_NET_LIB_CONFIG_IEEE802154_SETTINGS_H_
#define ZEPHYR_SUBSYS_NET_LIB_CONFIG_IEEE802154_SETTINGS_H_

/*
 * Copyright (c) 2017 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if defined(CONFIG_NET_L2_IEEE802154) && defined(CONFIG_NET_CONFIG_SETTINGS)
struct net_if;

int z_net_config_ieee802154_setup(struct net_if *iface);
#else
#define z_net_config_ieee802154_setup(...) 0
#endif

#endif /* ZEPHYR_SUBSYS_NET_LIB_CONFIG_IEEE802154_SETTINGS_H_ */
