/*
 * Copyright (c) 2019 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX internet address manipulation functions.
 * @ingroup bsd_sockets
 */

#ifndef ZEPHYR_INCLUDE_POSIX_ARPA_INET_H_
#define ZEPHYR_INCLUDE_POSIX_ARPA_INET_H_

#include <stddef.h>

#include <zephyr/posix/netinet/in.h>
#include <zephyr/posix/sys/socket.h>

#include <zephyr/net/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/** IPv4 address in binary form */
typedef uint32_t in_addr_t;

/**
 * @brief Convert an IPv4 dotted-decimal address string to binary form.
 *
 * See IEEE 1003.1
 */
in_addr_t inet_addr(const char *cp);

/**
 * @brief Convert an IPv4 address to a dotted-decimal address string.
 *
 * The returned string is stored in a static buffer which is overwritten
 * by subsequent calls.
 *
 * See IEEE 1003.1
 */
char *inet_ntoa(struct in_addr in);

/**
 * @brief Convert an IPv4 or IPv6 address from binary to text form.
 *
 * See IEEE 1003.1
 */
char *inet_ntop(sa_family_t family, const void *src, char *dst, size_t size);

/**
 * @brief Convert an IPv4 or IPv6 address from text to binary form.
 *
 * See IEEE 1003.1
 */
int inet_pton(sa_family_t family, const char *src, void *dst);

/** @brief Convert a 16-bit integer from network to host byte order */
#define ntohs(x)  net_ntohs(x)
/** @brief Convert a 32-bit integer from network to host byte order */
#define ntohl(x)  net_ntohl(x)
/** @brief Convert a 64-bit integer from network to host byte order */
#define ntohll(x) net_ntohll(x)
/** @brief Convert a 16-bit integer from host to network byte order */
#define htons(x)  net_htons(x)
/** @brief Convert a 32-bit integer from host to network byte order */
#define htonl(x)  net_htonl(x)
/** @brief Convert a 64-bit integer from host to network byte order */
#define htonll(x) net_htonll(x)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_ARPA_INET_H_ */
