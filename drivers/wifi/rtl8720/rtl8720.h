/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_WIFI_RTL8720_RTL8720_H_
#define ZEPHYR_DRIVERS_WIFI_RTL8720_RTL8720_H_

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/ring_buffer.h>

#define DT_DRV_COMPAT realtek_rtl8720_erpc

/*
 * eRPC service identifiers, from the shims generated out of the firmware IDL
 * (seeed-ambd-firmware, erpc_idl/).
 */
#define RTL8720_SVC_SYSTEM   1U
#define RTL8720_SVC_WIFI_DRV 14U
#define RTL8720_SVC_TCPIP    15U
#define RTL8720_SVC_LWIP     16U

/* rpc_system */
#define RTL8720_SYSTEM_VERSION 1U

/* rpc_wifi_drv */
#define RTL8720_WIFI_CONNECT             1U
#define RTL8720_WIFI_DISCONNECT          3U
#define RTL8720_WIFI_IS_CONNECTED_TO_AP  4U
#define RTL8720_WIFI_GET_MAC_ADDRESS     8U
#define RTL8720_WIFI_GET_AP_BSSID        15U
#define RTL8720_WIFI_GET_RSSI            19U
#define RTL8720_WIFI_GET_CHANNEL         21U
#define RTL8720_WIFI_ON                  27U
#define RTL8720_WIFI_OFF                 28U
#define RTL8720_WIFI_SET_MODE            29U
#define RTL8720_WIFI_GET_SETTING         41U
#define RTL8720_WIFI_SCAN_START          64U
#define RTL8720_WIFI_IS_SCANNING         65U
#define RTL8720_WIFI_SCAN_GET_AP_RECORDS 66U
#define RTL8720_WIFI_SCAN_GET_AP_NUM     67U

/* rpc_wifi_tcpip */
#define RTL8720_TCPIP_INIT        1U
#define RTL8720_TCPIP_STA_START   2U
#define RTL8720_TCPIP_GET_IP_INFO 7U
#define RTL8720_TCPIP_DHCPC_START 13U
#define RTL8720_TCPIP_DHCPC_STOP  14U

/* rpc_wifi_lwip */
#define RTL8720_LWIP_ACCEPT        1U
#define RTL8720_LWIP_BIND          2U
#define RTL8720_LWIP_SHUTDOWN      3U
#define RTL8720_LWIP_GETPEERNAME   4U
#define RTL8720_LWIP_GETSOCKNAME   5U
#define RTL8720_LWIP_GETSOCKOPT    6U
#define RTL8720_LWIP_SETSOCKOPT    7U
#define RTL8720_LWIP_CLOSE         8U
#define RTL8720_LWIP_CONNECT       9U
#define RTL8720_LWIP_LISTEN        10U
#define RTL8720_LWIP_AVAILABLE     11U
#define RTL8720_LWIP_RECV          12U
#define RTL8720_LWIP_RECVFROM      14U
#define RTL8720_LWIP_SEND          15U
#define RTL8720_LWIP_SENDTO        17U
#define RTL8720_LWIP_SOCKET        18U
#define RTL8720_LWIP_FCNTL         23U
#define RTL8720_LWIP_ERRNO         24U
#define RTL8720_LWIP_GETHOSTBYNAME 25U

/* Realtek operating modes, rtw_mode_t */
#define RTL8720_MODE_NONE   0U
#define RTL8720_MODE_STA    1U
#define RTL8720_MODE_AP     2U
#define RTL8720_MODE_STA_AP 3U

/* Realtek security types, rtw_security_t */
#define RTL8720_SEC_WEP_ENABLED  0x00000001U
#define RTL8720_SEC_TKIP_ENABLED 0x00000002U
#define RTL8720_SEC_AES_ENABLED  0x00000004U
#define RTL8720_SEC_WPA          0x00200000U
#define RTL8720_SEC_WPA2         0x00400000U
#define RTL8720_SEC_WPA3         0x00800000U

#define RTL8720_SECURITY_OPEN         0U
#define RTL8720_SECURITY_WPA2_AES_PSK (RTL8720_SEC_WPA2 | RTL8720_SEC_AES_ENABLED)
#define RTL8720_SECURITY_WPA3_AES_PSK (RTL8720_SEC_WPA3 | RTL8720_SEC_AES_ENABLED)
#define RTL8720_SECURITY_UNKNOWN      0xFFFFFFFFU

/* The firmware reports RTW_SUCCESS as 0 and RTW_ERROR as -1 */
#define RTL8720_RTW_SUCCESS 0

/* tcpip_adapter_if_t */
#define RTL8720_TCPIP_IF_STA 0U

/*
 * Wire size of the packed rtw_scan_result_t the firmware returns, and the
 * offsets of the fields this driver reads out of it. The struct is built
 * under #pragma pack(1) and every rtw_* enum is an unsigned long.
 */
#define RTL8720_SCAN_RESULT_SIZE  62U
#define RTL8720_SCAN_OFF_SSID_LEN 0U
#define RTL8720_SCAN_OFF_SSID     1U
#define RTL8720_SCAN_OFF_BSSID    34U
#define RTL8720_SCAN_OFF_RSSI     40U
#define RTL8720_SCAN_OFF_SECURITY 46U
#define RTL8720_SCAN_OFF_CHANNEL  54U
#define RTL8720_SCAN_OFF_BAND     58U

/* Wire size of tcpip_adapter_ip_info_t: three ip4_addr_t in host order */
#define RTL8720_IP_INFO_SIZE 12U

/*
 * The firmware speaks lwIP, whose address family numbering and sockaddr
 * layout both differ from the ones Zephyr uses.
 */
#define RTL8720_LWIP_AF_INET     2U
#define RTL8720_SOCKADDR_IN_SIZE 16U

/** @brief Buffer over which the eRPC codec reads and writes. */
struct rtl8720_codec {
	uint8_t *data;
	size_t size;
	size_t pos;
	bool err;
};

/** @brief Driver instance state. */
struct rtl8720_data {
	struct net_if *iface;

	/* serializes whole request/reply exchanges */
	struct k_mutex lock;
	/* released by the RX thread once a matching reply has landed */
	struct k_sem reply;
	/*
	 * released by the UART interrupt when it has buffered more bytes; binary,
	 * as one receive drains everything buffered so far
	 */
	struct k_sem rx_sem;

	uint32_t sequence;
	uint32_t pending_sequence;
	bool reply_valid;

	/* bytes handed over by the UART interrupt */
	struct ring_buf rx_ring;
	uint8_t rx_ring_buf[CONFIG_WIFI_RTL8720_RX_RING_SIZE];

	/* request being assembled and the reply it is waiting for */
	uint8_t tx_buf[CONFIG_WIFI_RTL8720_TX_BUF_SIZE];
	uint8_t rx_buf[CONFIG_WIFI_RTL8720_RX_BUF_SIZE];
	size_t rx_len;

	uint8_t mac[6];
	struct net_in_addr ip;
	struct net_in_addr gw;
	struct net_in_addr nm;

	scan_result_cb_t scan_cb;
	bool connected;

	struct k_work_q workq;
	struct k_work scan_work;
	struct k_work connect_work;
	struct k_work disconnect_work;
	struct wifi_connect_req_params connect_params;
	char connect_ssid[WIFI_SSID_MAX_LEN + 1];
	char connect_psk[WIFI_PSK_MAX_LEN + 1];
};

/**
 * @brief Set up the eRPC link to the module.
 *
 * Configures the UART, starts the receive thread and waits for the module to
 * answer a version request.
 *
 * @param dev RTL8720 device.
 *
 * @retval 0 On success.
 * @retval -ENODEV UART not ready.
 * @retval -ETIMEDOUT Module did not answer.
 */
int rtl8720_erpc_init(const struct device *dev);

/**
 * @brief Start assembling a request.
 *
 * Reserves room for the message header in the transmit buffer.
 *
 * @param data Driver instance state.
 * @param codec Codec to initialize over the transmit buffer.
 */
void rtl8720_erpc_request(struct rtl8720_data *data, struct rtl8720_codec *codec);

/**
 * @brief Send an assembled request and wait for its reply.
 *
 * The caller must hold no lock; this takes the instance lock for the whole
 * exchange. On success @p codec is re-pointed at the reply payload, positioned
 * on the first return value.
 *
 * @param data Driver instance state.
 * @param service eRPC service identifier.
 * @param request eRPC function identifier.
 * @param codec Codec holding the request, updated to hold the reply.
 *
 * @retval 0 On success.
 * @retval -EIO Codec overran a buffer or the module answered out of turn.
 * @retval -ETIMEDOUT Module did not answer in time.
 */
int rtl8720_erpc_call(struct rtl8720_data *data, uint8_t service, uint8_t request,
		      struct rtl8720_codec *codec);

void rtl8720_put_u8(struct rtl8720_codec *codec, uint8_t value);
void rtl8720_put_u16(struct rtl8720_codec *codec, uint16_t value);
void rtl8720_put_u32(struct rtl8720_codec *codec, uint32_t value);
void rtl8720_put_i32(struct rtl8720_codec *codec, int32_t value);
void rtl8720_put_bin(struct rtl8720_codec *codec, const void *value, size_t len);
void rtl8720_put_str(struct rtl8720_codec *codec, const char *value);
void rtl8720_put_nullable_str(struct rtl8720_codec *codec, const char *value);

uint16_t rtl8720_get_u16(struct rtl8720_codec *codec);
uint32_t rtl8720_get_u32(struct rtl8720_codec *codec);
int32_t rtl8720_get_i32(struct rtl8720_codec *codec);
int8_t rtl8720_get_i8(struct rtl8720_codec *codec);

/**
 * @brief Read a binary return value without copying it.
 *
 * @param codec Codec positioned on the value.
 * @param len Filled in with the length of the value.
 *
 * @return Pointer into the receive buffer, or NULL if the value is truncated.
 */
const uint8_t *rtl8720_get_bin(struct rtl8720_codec *codec, size_t *len);

/**
 * @brief Convert a Zephyr socket address to the lwIP layout the module wants.
 *
 * @param addr Address to convert, which must be NET_AF_INET.
 * @param out Buffer of at least RTL8720_SOCKADDR_IN_SIZE bytes.
 *
 * @retval 0 On success.
 * @retval -EAFNOSUPPORT Address is not IPv4.
 */
int rtl8720_sockaddr_to_lwip(const struct net_sockaddr *addr, uint8_t *out);

/**
 * @brief Convert an lwIP socket address from the module to Zephyr's layout.
 *
 * @param in Address as returned by the module.
 * @param len Length of @p in.
 * @param addr Address to fill in.
 * @param addrlen In: size of @p addr. Out: size actually used.
 *
 * @retval 0 On success.
 * @retval -EINVAL Address is truncated or not IPv4.
 */
int rtl8720_sockaddr_from_lwip(const uint8_t *in, size_t len, struct net_sockaddr *addr,
			       net_socklen_t *addrlen);

/** @brief Register the offloaded socket implementation on @p iface. */
void rtl8720_socket_offload_init(struct net_if *iface);

extern struct rtl8720_data rtl8720_driver_data;

#endif /* ZEPHYR_DRIVERS_WIFI_RTL8720_RTL8720_H_ */
