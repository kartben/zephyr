/*
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_NET_LIB_DNS_DNS_INTERNAL_H_
#define ZEPHYR_SUBSYS_NET_LIB_DNS_DNS_INTERNAL_H_

#include <zephyr/types.h>
#include <zephyr/net_buf.h>
#include <zephyr/net/dns_resolve.h>

#include "dns_pack.h"

#if defined(CONFIG_NET_TEST)
int dns_validate_msg(struct dns_resolve_context *ctx,
		     struct dns_msg_t *dns_msg,
		     uint16_t *dns_id,
		     int *query_idx,
		     struct net_buf *dns_cname,
		     uint16_t *query_hash,
		     int recv_server_idx);
#endif

#endif /* ZEPHYR_SUBSYS_NET_LIB_DNS_DNS_INTERNAL_H_ */
