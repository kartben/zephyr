/*
 * Copyright (c) 2019 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX network interface name mapping functions.
 * @ingroup bsd_sockets
 */

#ifndef ZEPHYR_INCLUDE_POSIX_NET_IF_H_
#define ZEPHYR_INCLUDE_POSIX_NET_IF_H_

#ifdef CONFIG_NET_INTERFACE_NAME_LEN
/** Maximum length of a network interface name */
#define IF_NAMESIZE CONFIG_NET_INTERFACE_NAME_LEN
#else
/** Maximum length of a network interface name */
#define IF_NAMESIZE 1
#endif

#if !defined(IFNAMSIZ)
/** Alias for @ref IF_NAMESIZE */
#define IFNAMSIZ IF_NAMESIZE
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Network interface name and index pair
 */
struct if_nameindex {
	unsigned int if_index; /**< Numeric index of the interface */
	char *if_name;         /**< Null-terminated name of the interface */
};

/**
 * @brief Map a network interface index to its corresponding name.
 *
 * See IEEE 1003.1
 */
char *if_indextoname(unsigned int ifindex, char *ifname);

/**
 * @brief Free the array of interface names and indexes returned by if_nameindex().
 *
 * See IEEE 1003.1
 */
void if_freenameindex(struct if_nameindex *ptr);

/**
 * @brief Get a list of the names and indexes of all network interfaces.
 *
 * See IEEE 1003.1
 */
struct if_nameindex *if_nameindex(void);

/**
 * @brief Map a network interface name to its corresponding index.
 *
 * See IEEE 1003.1
 */
unsigned int if_nametoindex(const char *ifname);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_NET_IF_H_ */
