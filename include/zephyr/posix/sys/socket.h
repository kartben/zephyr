/*
 * Copyright (c) 2019 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX sockets API declarations.
 * @ingroup bsd_sockets
 */

#ifndef ZEPHYR_INCLUDE_POSIX_SYS_SOCKET_H_
#define ZEPHYR_INCLUDE_POSIX_SYS_SOCKET_H_

#include <sys/types.h>

/** @cond INTERNAL_HIDDEN */
#undef ZEPHYR_INCLUDE_NET_COMPAT_MODE_SYMBOLS
#define ZEPHYR_INCLUDE_NET_COMPAT_MODE_SYMBOLS
/** @endcond */
#include <zephyr/net/socket.h>
#undef ZEPHYR_INCLUDE_NET_COMPAT_MODE_SYMBOLS

#ifdef __cplusplus
extern "C" {
#endif

/** Structure used with the SO_LINGER socket option */
struct linger {
	int  l_onoff;  /**< Indicates whether linger option is enabled */
	int  l_linger; /**< Linger time, in seconds */
};

#if !defined(CONFIG_NET_NAMESPACE_COMPAT_MODE)
/** Type describing the length of a socket address */
typedef uint32_t socklen_t;
struct msghdr;
struct sockaddr;

/** Read data without removing it from the socket input queue */
#define MSG_PEEK     ZSOCK_MSG_PEEK
/** Return the real length of the datagram, even when longer than the passed buffer */
#define MSG_TRUNC    ZSOCK_MSG_TRUNC
/** Override the operation to be non-blocking */
#define MSG_DONTWAIT ZSOCK_MSG_DONTWAIT
/** Block until the full amount of data can be returned */
#define MSG_WAITALL  ZSOCK_MSG_WAITALL

/** Shut down the socket for reading */
#define SHUT_RD   ZSOCK_SHUT_RD
/** Shut down the socket for writing */
#define SHUT_WR   ZSOCK_SHUT_WR
/** Shut down the socket for both reading and writing */
#define SHUT_RDWR ZSOCK_SHUT_RDWR
#endif

/**
 * @brief Accept a new connection on a socket
 *
 * See IEEE 1003.1
 */
int accept(int sock, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Bind a name to a socket
 *
 * See IEEE 1003.1
 */
int bind(int sock, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief Connect a socket
 *
 * See IEEE 1003.1
 */
int connect(int sock, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief Get the name of the peer socket
 *
 * See IEEE 1003.1
 */
int getpeername(int sock, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Get the socket name
 *
 * See IEEE 1003.1
 */
int getsockname(int sock, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Get the socket options
 *
 * See IEEE 1003.1
 */
int getsockopt(int sock, int level, int optname, void *optval, socklen_t *optlen);

/**
 * @brief Listen for socket connections and limit the queue of incoming connections
 *
 * See IEEE 1003.1
 */
int listen(int sock, int backlog);

/**
 * @brief Receive a message from a connected socket
 *
 * See IEEE 1003.1
 */
ssize_t recv(int sock, void *buf, size_t max_len, int flags);

/**
 * @brief Receive a message from a socket
 *
 * See IEEE 1003.1
 */
ssize_t recvfrom(int sock, void *buf, size_t max_len, int flags, struct sockaddr *src_addr,
		 socklen_t *addrlen);

/**
 * @brief Receive a message from a socket using a message structure
 *
 * See IEEE 1003.1
 */
ssize_t recvmsg(int sock, struct msghdr *msg, int flags);

/**
 * @brief Send a message on a connected socket
 *
 * See IEEE 1003.1
 */
ssize_t send(int sock, const void *buf, size_t len, int flags);

/**
 * @brief Send a message on a socket using a message structure
 *
 * See IEEE 1003.1
 */
ssize_t sendmsg(int sock, const struct msghdr *message, int flags);

/**
 * @brief Send a message on a socket to a specified address
 *
 * See IEEE 1003.1
 */
ssize_t sendto(int sock, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
	       socklen_t addrlen);

/**
 * @brief Set the socket options
 *
 * See IEEE 1003.1
 */
int setsockopt(int sock, int level, int optname, const void *optval, socklen_t optlen);

/**
 * @brief Shut down socket send and receive operations
 *
 * See IEEE 1003.1
 */
int shutdown(int sock, int how);

/**
 * @brief Determine whether a socket is at the out-of-band mark
 *
 * See IEEE 1003.1
 */
int sockatmark(int s);

/**
 * @brief Create an endpoint for communication
 *
 * See IEEE 1003.1
 */
int socket(int family, int type, int proto);

/**
 * @brief Create a pair of connected sockets
 *
 * See IEEE 1003.1
 */
int socketpair(int family, int type, int proto, int sv[2]);

#ifdef __cplusplus
}
#endif

#endif	/* ZEPHYR_INCLUDE_POSIX_SYS_SOCKET_H_ */
