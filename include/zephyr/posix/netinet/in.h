/*
 * Copyright (c) 2019 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX internet address family definitions.
 * @ingroup bsd_sockets
 */

#ifndef ZEPHYR_INCLUDE_POSIX_NETINET_IN_H_
#define ZEPHYR_INCLUDE_POSIX_NETINET_IN_H_

#include <stdint.h>

#include <zephyr/net/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Internet port number */
typedef uint16_t in_port_t;
/** IPv4 address in binary form */
typedef uint32_t in_addr_t;

/** IPv4 address structure */
#define in_addr  net_in_addr
/** IPv6 address structure */
#define in6_addr net_in6_addr

/** Size of the string form of an IPv4 address, including the terminating null byte */
#define INET_ADDRSTRLEN  NET_INET_ADDRSTRLEN
/** Size of the string form of an IPv6 address, including the terminating null byte */
#define INET6_ADDRSTRLEN NET_INET6_ADDRSTRLEN

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_NETINET_IN_H_ */
