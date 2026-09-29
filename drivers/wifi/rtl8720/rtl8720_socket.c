/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(wifi_rtl8720, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/net/socket_offload.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "sockets_internal.h"
#include "rtl8720.h"

/*
 * The file descriptor table treats a stored value of 0 as unused, so module
 * socket descriptors are kept biased by one.
 */
#define SD_TO_OBJ(sd)  ((void *)((intptr_t)(sd) + 1))
#define OBJ_TO_SD(obj) (((intptr_t)(obj)) - 1)

/* lwIP returns ERR_OK as 0 for the name resolution calls */
#define RTL8720_ERR_OK 0

static const struct socket_op_vtable rtl8720_socket_fd_op_vtable;

/*
 * Sockets the application has put in non-blocking mode, keyed by the module's
 * own descriptor. lwIP hands out small descriptors, so a bitmask covers them.
 */
static atomic_t rtl8720_nonblock_mask;

#define RTL8720_NONBLOCK_MAX 32

static bool rtl8720_is_nonblock(int sd)
{
	if (sd < 0 || sd >= RTL8720_NONBLOCK_MAX) {
		return false;
	}

	return (atomic_get(&rtl8720_nonblock_mask) & BIT(sd)) != 0;
}

static void rtl8720_set_nonblock(int sd, bool nonblock)
{
	if (sd < 0 || sd >= RTL8720_NONBLOCK_MAX) {
		return;
	}

	if (nonblock) {
		(void)atomic_or(&rtl8720_nonblock_mask, BIT(sd));
	} else {
		(void)atomic_and(&rtl8720_nonblock_mask, ~BIT(sd));
	}
}

int rtl8720_sockaddr_to_lwip(const struct net_sockaddr *addr, uint8_t *out)
{
	const struct net_sockaddr_in *in = (const struct net_sockaddr_in *)addr;

	if (addr->sa_family != NET_AF_INET) {
		return -EAFNOSUPPORT;
	}

	memset(out, 0, RTL8720_SOCKADDR_IN_SIZE);

	/* lwIP leads its sockaddr with a length byte and numbers AF_INET as 2 */
	out[0] = RTL8720_SOCKADDR_IN_SIZE;
	out[1] = RTL8720_LWIP_AF_INET;
	memcpy(&out[2], &in->sin_port, sizeof(in->sin_port));
	memcpy(&out[4], &in->sin_addr.s_addr, sizeof(in->sin_addr.s_addr));

	return 0;
}

int rtl8720_sockaddr_from_lwip(const uint8_t *in, size_t len, struct net_sockaddr *addr,
			       net_socklen_t *addrlen)
{
	struct net_sockaddr_in out = {0};

	if (len < RTL8720_SOCKADDR_IN_SIZE || in[1] != RTL8720_LWIP_AF_INET) {
		return -EINVAL;
	}

	out.sin_family = NET_AF_INET;
	memcpy(&out.sin_port, &in[2], sizeof(out.sin_port));
	memcpy(&out.sin_addr.s_addr, &in[4], sizeof(out.sin_addr.s_addr));

	memcpy(addr, &out, MIN(*addrlen, sizeof(out)));
	*addrlen = sizeof(out);

	return 0;
}

/* Turns the module's return value into a Zephyr socket API failure */
static int rtl8720_socket_fail(int err)
{
	errno = (err == 0) ? EIO : err;
	return -1;
}

static int rtl8720_lwip_errno(struct rtl8720_data *data)
{
	struct rtl8720_codec codec;
	int32_t result;

	rtl8720_erpc_request(data, &codec);

	if (rtl8720_erpc_call(data, RTL8720_SVC_LWIP, RTL8720_LWIP_ERRNO, &codec) < 0) {
		return EIO;
	}

	result = rtl8720_get_i32(&codec);
	if (codec.err || result <= 0) {
		return EIO;
	}

	return (int)result;
}

static int rtl8720_socket_open(int family, int type, int proto)
{
	struct rtl8720_data *data = &rtl8720_driver_data;
	struct rtl8720_codec codec;
	int32_t sd;
	int ret;

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_i32(&codec, RTL8720_LWIP_AF_INET);
	rtl8720_put_i32(&codec, type);
	rtl8720_put_i32(&codec, proto);

	ARG_UNUSED(family);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_LWIP, RTL8720_LWIP_SOCKET, &codec);
	if (ret < 0) {
		return ret;
	}

	sd = rtl8720_get_i32(&codec);
	if (codec.err) {
		return -EIO;
	}

	if (sd < 0) {
		return -rtl8720_lwip_errno(data);
	}

	return (int)sd;
}

static int rtl8720_socket_close(void *obj)
{
	struct rtl8720_data *data = &rtl8720_driver_data;
	struct rtl8720_codec codec;
	int32_t result;
	int ret;

	rtl8720_set_nonblock((int)OBJ_TO_SD(obj), false);

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_i32(&codec, (int32_t)OBJ_TO_SD(obj));

	ret = rtl8720_erpc_call(data, RTL8720_SVC_LWIP, RTL8720_LWIP_CLOSE, &codec);
	if (ret < 0) {
		return rtl8720_socket_fail(EIO);
	}

	result = rtl8720_get_i32(&codec);
	if (codec.err || result < 0) {
		return rtl8720_socket_fail(rtl8720_lwip_errno(data));
	}

	return 0;
}

static int rtl8720_socket_addr_call(void *obj, uint8_t request, const struct net_sockaddr *addr,
				    net_socklen_t addrlen)
{
	struct rtl8720_data *data = &rtl8720_driver_data;
	uint8_t lwip_addr[RTL8720_SOCKADDR_IN_SIZE];
	struct rtl8720_codec codec;
	int32_t result;
	int ret;

	ARG_UNUSED(addrlen);

	ret = rtl8720_sockaddr_to_lwip(addr, lwip_addr);
	if (ret < 0) {
		return rtl8720_socket_fail(-ret);
	}

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_i32(&codec, (int32_t)OBJ_TO_SD(obj));
	rtl8720_put_bin(&codec, lwip_addr, sizeof(lwip_addr));
	rtl8720_put_u32(&codec, sizeof(lwip_addr));

	ret = rtl8720_erpc_call(data, RTL8720_SVC_LWIP, request, &codec);
	if (ret < 0) {
		return rtl8720_socket_fail(EIO);
	}

	result = rtl8720_get_i32(&codec);
	if (codec.err || result < 0) {
		return rtl8720_socket_fail(rtl8720_lwip_errno(data));
	}

	return 0;
}

static int rtl8720_socket_bind(void *obj, const struct net_sockaddr *addr, net_socklen_t addrlen)
{
	return rtl8720_socket_addr_call(obj, RTL8720_LWIP_BIND, addr, addrlen);
}

static int rtl8720_socket_connect(void *obj, const struct net_sockaddr *addr, net_socklen_t addrlen)
{
	return rtl8720_socket_addr_call(obj, RTL8720_LWIP_CONNECT, addr, addrlen);
}

static int rtl8720_socket_listen(void *obj, int backlog)
{
	struct rtl8720_data *data = &rtl8720_driver_data;
	struct rtl8720_codec codec;
	int32_t result;
	int ret;

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_i32(&codec, (int32_t)OBJ_TO_SD(obj));
	rtl8720_put_i32(&codec, backlog);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_LWIP, RTL8720_LWIP_LISTEN, &codec);
	if (ret < 0) {
		return rtl8720_socket_fail(EIO);
	}

	result = rtl8720_get_i32(&codec);
	if (codec.err || result < 0) {
		return rtl8720_socket_fail(rtl8720_lwip_errno(data));
	}

	return 0;
}

static int rtl8720_socket_accept(void *obj, struct net_sockaddr *addr, net_socklen_t *addrlen)
{
	struct rtl8720_data *data = &rtl8720_driver_data;
	uint8_t lwip_addr[RTL8720_SOCKADDR_IN_SIZE] = {0};
	struct rtl8720_codec codec;
	int32_t result;
	int fd;
	int ret;

	fd = zvfs_reserve_fd();
	if (fd < 0) {
		return rtl8720_socket_fail(EMFILE);
	}

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_i32(&codec, (int32_t)OBJ_TO_SD(obj));
	rtl8720_put_bin(&codec, lwip_addr, sizeof(lwip_addr));
	rtl8720_put_u32(&codec, sizeof(lwip_addr));

	ret = rtl8720_erpc_call(data, RTL8720_SVC_LWIP, RTL8720_LWIP_ACCEPT, &codec);
	if (ret < 0) {
		zvfs_free_fd(fd);
		return rtl8720_socket_fail(EIO);
	}

	/* The module reports the address length back but not the address */
	(void)rtl8720_get_u32(&codec);
	result = rtl8720_get_i32(&codec);

	if (codec.err || result < 0) {
		zvfs_free_fd(fd);
		return rtl8720_socket_fail(rtl8720_lwip_errno(data));
	}

	if (addr != NULL && addrlen != NULL) {
		memset(addr, 0, *addrlen);
		addr->sa_family = NET_AF_INET;
		*addrlen = sizeof(struct net_sockaddr_in);
	}

	zvfs_finalize_typed_fd(fd, SD_TO_OBJ(result),
			       (const struct fd_op_vtable *)&rtl8720_socket_fd_op_vtable,
			       ZVFS_MODE_IFSOCK);

	return fd;
}

static ssize_t rtl8720_socket_sendto(void *obj, const void *buf, size_t len, int flags,
				     const struct net_sockaddr *to, net_socklen_t tolen)
{
	struct rtl8720_data *data = &rtl8720_driver_data;
	uint8_t lwip_addr[RTL8720_SOCKADDR_IN_SIZE];
	struct rtl8720_codec codec;
	size_t chunk;
	int32_t result;
	int ret;

	ARG_UNUSED(tolen);

	chunk = MIN(len, CONFIG_WIFI_RTL8720_MTU);

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_i32(&codec, (int32_t)OBJ_TO_SD(obj));
	rtl8720_put_bin(&codec, buf, chunk);
	rtl8720_put_i32(&codec, flags);

	if (to != NULL) {
		ret = rtl8720_sockaddr_to_lwip(to, lwip_addr);
		if (ret < 0) {
			return rtl8720_socket_fail(-ret);
		}

		rtl8720_put_bin(&codec, lwip_addr, sizeof(lwip_addr));
		rtl8720_put_u32(&codec, sizeof(lwip_addr));
	}

	ret = rtl8720_erpc_call(data, RTL8720_SVC_LWIP,
				(to != NULL) ? RTL8720_LWIP_SENDTO : RTL8720_LWIP_SEND, &codec);
	if (ret < 0) {
		return rtl8720_socket_fail(EIO);
	}

	result = rtl8720_get_i32(&codec);
	if (codec.err) {
		return rtl8720_socket_fail(EIO);
	}

	if (result < 0) {
		return rtl8720_socket_fail(rtl8720_lwip_errno(data));
	}

	return (ssize_t)result;
}

/*
 * Runs one receive exchange. Returns the number of bytes handed over, or a
 * negative errno. The module answers a request that found nothing with the
 * errno it would have set locally, so EAGAIN here means "nothing yet" rather
 * than a failure.
 */
static ssize_t rtl8720_socket_recv_once(void *obj, void *buf, size_t max_len, int flags,
					struct net_sockaddr *from, net_socklen_t *fromlen)
{
	struct rtl8720_data *data = &rtl8720_driver_data;
	struct rtl8720_codec codec;
	const uint8_t *payload;
	size_t payload_len;
	size_t chunk;
	int32_t result;
	int ret;

	chunk = MIN(max_len, CONFIG_WIFI_RTL8720_MTU);

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_i32(&codec, (int32_t)OBJ_TO_SD(obj));
	rtl8720_put_u32(&codec, chunk);
	rtl8720_put_i32(&codec, flags);

	if (from != NULL) {
		rtl8720_put_u32(&codec, RTL8720_SOCKADDR_IN_SIZE);
	}

	rtl8720_put_u32(&codec, CONFIG_WIFI_RTL8720_RECV_TIMEOUT);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_LWIP,
				(from != NULL) ? RTL8720_LWIP_RECVFROM : RTL8720_LWIP_RECV, &codec);
	if (ret < 0) {
		return -EIO;
	}

	payload = rtl8720_get_bin(&codec, &payload_len);
	if (codec.err) {
		return -EIO;
	}

	if (from != NULL) {
		const uint8_t *lwip_addr;
		size_t lwip_addr_len;

		lwip_addr = rtl8720_get_bin(&codec, &lwip_addr_len);
		(void)rtl8720_get_u32(&codec);

		if (!codec.err && fromlen != NULL &&
		    rtl8720_sockaddr_from_lwip(lwip_addr, lwip_addr_len, from, fromlen) < 0) {
			LOG_DBG("Peer address of a datagram was not usable");
		}
	}

	result = rtl8720_get_i32(&codec);
	if (codec.err) {
		return -EIO;
	}

	if (result < 0) {
		return -rtl8720_lwip_errno(data);
	}

	if (payload_len > max_len) {
		payload_len = max_len;
	}

	if (payload_len > 0U) {
		memcpy(buf, payload, payload_len);
	}

	return (ssize_t)payload_len;
}

static ssize_t rtl8720_socket_recvfrom(void *obj, void *buf, size_t max_len, int flags,
				       struct net_sockaddr *from, net_socklen_t *fromlen)
{
	bool nonblock =
		rtl8720_is_nonblock((int)OBJ_TO_SD(obj)) || (flags & ZSOCK_MSG_DONTWAIT) != 0;
	ssize_t ret;

	/*
	 * A receive request only blocks on the module for its own timeout, so
	 * a blocking socket keeps asking until something arrives.
	 */
	do {
		ret = rtl8720_socket_recv_once(obj, buf, max_len, flags, from, fromlen);
		if (ret >= 0) {
			return ret;
		}

		if (ret != -EAGAIN && ret != -EWOULDBLOCK) {
			return rtl8720_socket_fail((int)-ret);
		}
	} while (!nonblock);

	return rtl8720_socket_fail(EAGAIN);
}

static int rtl8720_socket_setsockopt(void *obj, int level, int optname, const void *optval,
				     net_socklen_t optlen)
{
	struct rtl8720_data *data = &rtl8720_driver_data;
	struct rtl8720_codec codec;
	int32_t result;
	int ret;

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_i32(&codec, (int32_t)OBJ_TO_SD(obj));
	rtl8720_put_i32(&codec, level);
	rtl8720_put_i32(&codec, optname);
	rtl8720_put_bin(&codec, optval, optlen);
	rtl8720_put_u32(&codec, optlen);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_LWIP, RTL8720_LWIP_SETSOCKOPT, &codec);
	if (ret < 0) {
		return rtl8720_socket_fail(EIO);
	}

	result = rtl8720_get_i32(&codec);
	if (codec.err || result < 0) {
		return rtl8720_socket_fail(rtl8720_lwip_errno(data));
	}

	return 0;
}

static int rtl8720_socket_getsockopt(void *obj, int level, int optname, void *optval,
				     net_socklen_t *optlen)
{
	struct rtl8720_data *data = &rtl8720_driver_data;
	struct rtl8720_codec codec;
	const uint8_t *value;
	size_t value_len;
	int32_t result;
	int ret;

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_i32(&codec, (int32_t)OBJ_TO_SD(obj));
	rtl8720_put_i32(&codec, level);
	rtl8720_put_i32(&codec, optname);
	rtl8720_put_bin(&codec, optval, *optlen);
	rtl8720_put_u32(&codec, *optlen);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_LWIP, RTL8720_LWIP_GETSOCKOPT, &codec);
	if (ret < 0) {
		return rtl8720_socket_fail(EIO);
	}

	value = rtl8720_get_bin(&codec, &value_len);
	(void)rtl8720_get_u32(&codec);
	result = rtl8720_get_i32(&codec);

	if (codec.err || result < 0) {
		return rtl8720_socket_fail(rtl8720_lwip_errno(data));
	}

	if (value_len > *optlen) {
		value_len = *optlen;
	}

	memcpy(optval, value, value_len);
	*optlen = (net_socklen_t)value_len;

	return 0;
}

/*
 * Number of bytes the module has buffered for this socket, which is what
 * poll() is really asking about.
 */
static int rtl8720_socket_available(void *obj)
{
	struct rtl8720_data *data = &rtl8720_driver_data;
	struct rtl8720_codec codec;
	int32_t result;

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_i32(&codec, (int32_t)OBJ_TO_SD(obj));

	if (rtl8720_erpc_call(data, RTL8720_SVC_LWIP, RTL8720_LWIP_AVAILABLE, &codec) < 0) {
		return -EIO;
	}

	result = rtl8720_get_i32(&codec);

	return codec.err ? -EIO : (int)result;
}

static int rtl8720_socket_poll(struct zsock_pollfd *fds, int nfds, int timeout)
{
	k_timepoint_t deadline;
	int ready = 0;
	int i;

	deadline = sys_timepoint_calc((timeout < 0) ? K_FOREVER : K_MSEC(timeout));

	while (true) {
		for (i = 0; i < nfds; i++) {
			const struct fd_op_vtable *vtable;
			struct k_mutex *lock;
			void *obj;

			fds[i].revents = 0;

			if (fds[i].fd < 0) {
				continue;
			}

			obj = zvfs_get_fd_obj_and_vtable(fds[i].fd, &vtable, &lock);
			if (obj == NULL ||
			    vtable != (const struct fd_op_vtable *)&rtl8720_socket_fd_op_vtable) {
				fds[i].revents = ZSOCK_POLLNVAL;
				ready++;
				continue;
			}

			if ((fds[i].events & ZSOCK_POLLOUT) != 0) {
				/* The module buffers transmits, so it is always writable */
				fds[i].revents |= ZSOCK_POLLOUT;
			}

			if ((fds[i].events & ZSOCK_POLLIN) != 0) {
				int available = rtl8720_socket_available(obj);

				if (available < 0) {
					fds[i].revents |= ZSOCK_POLLERR;
				} else if (available > 0) {
					fds[i].revents |= ZSOCK_POLLIN;
				}
			}

			if (fds[i].revents != 0) {
				ready++;
			}
		}

		if (ready > 0 || sys_timepoint_expired(deadline)) {
			return ready;
		}

		k_sleep(K_MSEC(CONFIG_WIFI_RTL8720_POLL_INTERVAL));
	}
}

static int rtl8720_socket_ioctl(void *obj, unsigned int request, va_list args)
{
	int sd = (int)OBJ_TO_SD(obj);

	switch (request) {
	case ZVFS_F_GETFL:
		return rtl8720_is_nonblock(sd) ? ZVFS_O_NONBLOCK : 0;

	case ZVFS_F_SETFL:
		rtl8720_set_nonblock(sd, (va_arg(args, int) & ZVFS_O_NONBLOCK) != 0);
		return 0;

	case ZFD_IOCTL_POLL_PREPARE:
		return -EXDEV;

	case ZFD_IOCTL_POLL_UPDATE:
		return -EOPNOTSUPP;

	case ZFD_IOCTL_POLL_OFFLOAD: {
		struct zsock_pollfd *fds;
		int nfds;
		int timeout;

		fds = va_arg(args, struct zsock_pollfd *);
		nfds = va_arg(args, int);
		timeout = va_arg(args, int);

		return rtl8720_socket_poll(fds, nfds, timeout);
	}

	default:
		errno = EINVAL;
		return -1;
	}
}

static ssize_t rtl8720_socket_read(void *obj, void *buffer, size_t count)
{
	return rtl8720_socket_recvfrom(obj, buffer, count, 0, NULL, NULL);
}

static ssize_t rtl8720_socket_write(void *obj, const void *buffer, size_t count)
{
	return rtl8720_socket_sendto(obj, buffer, count, 0, NULL, 0);
}

static const struct socket_op_vtable rtl8720_socket_fd_op_vtable = {
	.fd_vtable = {
		.read = rtl8720_socket_read,
		.write = rtl8720_socket_write,
		.close = rtl8720_socket_close,
		.ioctl = rtl8720_socket_ioctl,
	},
	.bind = rtl8720_socket_bind,
	.connect = rtl8720_socket_connect,
	.listen = rtl8720_socket_listen,
	.accept = rtl8720_socket_accept,
	.sendto = rtl8720_socket_sendto,
	.recvfrom = rtl8720_socket_recvfrom,
	.getsockopt = rtl8720_socket_getsockopt,
	.setsockopt = rtl8720_socket_setsockopt,
};

static bool rtl8720_socket_is_supported(int family, int type, int proto)
{
	if (family != NET_AF_INET) {
		return false;
	}

	if (type != SOCK_STREAM && type != SOCK_DGRAM) {
		return false;
	}

	return true;
}

static int rtl8720_socket_create(int family, int type, int proto)
{
	int fd = zvfs_reserve_fd();
	int sd;

	if (fd < 0) {
		return -1;
	}

	sd = rtl8720_socket_open(family, type, proto);
	if (sd < 0) {
		zvfs_free_fd(fd);
		errno = -sd;
		return -1;
	}

	zvfs_finalize_typed_fd(fd, SD_TO_OBJ(sd),
			       (const struct fd_op_vtable *)&rtl8720_socket_fd_op_vtable,
			       ZVFS_MODE_IFSOCK);

	return fd;
}

static int rtl8720_getaddrinfo(const char *node, const char *service,
			       const struct zsock_addrinfo *hints, struct zsock_addrinfo **res)
{
	struct rtl8720_data *data = &rtl8720_driver_data;
	struct net_sockaddr_in *ai_addr;
	struct zsock_addrinfo *ai;
	struct rtl8720_codec codec;
	const uint8_t *addr;
	unsigned long port = 0;
	size_t addr_len;
	int32_t result;
	int ret;

	if (node == NULL) {
		return DNS_EAI_NONAME;
	}

	if (service != NULL) {
		port = strtoul(service, NULL, 10);
		if (port > USHRT_MAX) {
			return DNS_EAI_SERVICE;
		}
	}

	if (hints != NULL && hints->ai_family == NET_AF_INET6) {
		return DNS_EAI_ADDRFAMILY;
	}

	ai = calloc(1, sizeof(*ai));
	if (ai == NULL) {
		return DNS_EAI_MEMORY;
	}

	ai_addr = calloc(1, sizeof(*ai_addr));
	if (ai_addr == NULL) {
		free(ai);
		return DNS_EAI_MEMORY;
	}

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_str(&codec, node);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_LWIP, RTL8720_LWIP_GETHOSTBYNAME, &codec);
	if (ret < 0) {
		goto fail;
	}

	addr = rtl8720_get_bin(&codec, &addr_len);
	result = rtl8720_get_i8(&codec);

	if (codec.err || result != RTL8720_ERR_OK || addr_len < sizeof(ai_addr->sin_addr)) {
		goto fail;
	}

	ai->ai_family = NET_AF_INET;
	ai->ai_socktype =
		(hints != NULL && hints->ai_socktype != 0) ? hints->ai_socktype : SOCK_STREAM;
	ai->ai_protocol = (ai->ai_socktype == SOCK_DGRAM) ? NET_IPPROTO_UDP : NET_IPPROTO_TCP;
	ai->ai_addrlen = sizeof(*ai_addr);
	ai->ai_addr = (struct net_sockaddr *)ai_addr;

	ai_addr->sin_family = NET_AF_INET;
	ai_addr->sin_port = net_htons((uint16_t)port);
	/* The first word of an lwIP ip_addr_t is the IPv4 address, already in
	 * network byte order.
	 */
	memcpy(&ai_addr->sin_addr.s_addr, addr, sizeof(ai_addr->sin_addr.s_addr));

	if (ai_addr->sin_addr.s_addr == 0U) {
		goto fail;
	}

	*res = ai;

	return 0;

fail:
	free(ai_addr);
	free(ai);

	return DNS_EAI_FAIL;
}

static void rtl8720_freeaddrinfo(struct zsock_addrinfo *res)
{
	if (res == NULL) {
		return;
	}

	free(res->ai_addr);
	free(res);
}

static const struct socket_dns_offload rtl8720_dns_ops = {
	.getaddrinfo = rtl8720_getaddrinfo,
	.freeaddrinfo = rtl8720_freeaddrinfo,
};

void rtl8720_socket_offload_init(struct net_if *iface)
{
	net_if_socket_offload_set(iface, rtl8720_socket_create);
	socket_offload_dns_register(&rtl8720_dns_ops);
}

NET_SOCKET_OFFLOAD_REGISTER(rtl8720, CONFIG_NET_SOCKETS_OFFLOAD_PRIORITY, NET_AF_INET,
			    rtl8720_socket_is_supported, rtl8720_socket_create);
