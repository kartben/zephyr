/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_rtl8720, CONFIG_WIFI_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_offload.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_utils.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <string.h>

#include "rtl8720.h"

struct rtl8720_data rtl8720_driver_data;

K_KERNEL_STACK_DEFINE(rtl8720_workq_stack, CONFIG_WIFI_RTL8720_WORKQ_STACK_SIZE);

#if DT_INST_NODE_HAS_PROP(0, power_gpios)
static const struct gpio_dt_spec rtl8720_power = GPIO_DT_SPEC_INST_GET(0, power_gpios);
#endif

/*
 * Hold the module in reset briefly and give its firmware time to come up
 * before the first request goes out.
 */
static int rtl8720_reset(void)
{
#if DT_INST_NODE_HAS_PROP(0, power_gpios)
	int ret;

	ret = gpio_pin_configure_dt(&rtl8720_power, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure the chip enable line: %d", ret);
		return ret;
	}

	k_sleep(K_MSEC(CONFIG_WIFI_RTL8720_RESET_HOLD));

	ret = gpio_pin_set_dt(&rtl8720_power, 1);
	if (ret < 0) {
		LOG_ERR("Failed to release the chip enable line: %d", ret);
		return ret;
	}

	k_sleep(K_MSEC(CONFIG_WIFI_RTL8720_BOOT_DELAY));
#endif

	return 0;
}

/*
 * The module answers every call with its return values followed by the
 * function result, so a call that only yields a result reduces to this.
 */
static int rtl8720_call_i32(struct rtl8720_data *data, uint8_t service, uint8_t request,
			    const uint32_t *args, size_t argc, int32_t *result)
{
	struct rtl8720_codec codec;
	size_t i;
	int ret;

	rtl8720_erpc_request(data, &codec);

	for (i = 0U; i < argc; i++) {
		rtl8720_put_u32(&codec, args[i]);
	}

	ret = rtl8720_erpc_call(data, service, request, &codec);
	if (ret < 0) {
		return ret;
	}

	*result = rtl8720_get_i32(&codec);

	return codec.err ? -EIO : 0;
}

/* The firmware turns the radio on at boot and refuses a second wifi_on */
static int rtl8720_wifi_on(struct rtl8720_data *data, uint32_t mode)
{
	int32_t result;
	int ret;

	ret = rtl8720_call_i32(data, RTL8720_SVC_WIFI_DRV, RTL8720_WIFI_OFF, NULL, 0U, &result);
	if (ret < 0) {
		return ret;
	}

	k_sleep(K_MSEC(20));

	ret = rtl8720_call_i32(data, RTL8720_SVC_WIFI_DRV, RTL8720_WIFI_ON, &mode, 1U, &result);
	if (ret < 0) {
		return ret;
	}

	return (result == RTL8720_RTW_SUCCESS) ? 0 : -EIO;
}

static int rtl8720_tcpip_init(struct rtl8720_data *data)
{
	int32_t result;
	int ret;

	ret = rtl8720_call_i32(data, RTL8720_SVC_TCPIP, RTL8720_TCPIP_INIT, NULL, 0U, &result);
	if (ret < 0) {
		return ret;
	}

	return (result == 0) ? 0 : -EIO;
}

static int rtl8720_tcpip_dhcpc_start(struct rtl8720_data *data)
{
	uint32_t iface = RTL8720_TCPIP_IF_STA;
	int32_t result;
	int ret;

	ret = rtl8720_call_i32(data, RTL8720_SVC_TCPIP, RTL8720_TCPIP_DHCPC_START, &iface, 1U,
			       &result);
	if (ret < 0) {
		return ret;
	}

	return (result == 0) ? 0 : -EIO;
}

/* Reads the MAC back as the text form the firmware hands out, "xx:xx:..." */
static int rtl8720_read_mac(struct rtl8720_data *data)
{
	struct rtl8720_codec codec;
	char mac[18];
	size_t i;
	int ret;

	rtl8720_erpc_request(data, &codec);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_WIFI_DRV, RTL8720_WIFI_GET_MAC_ADDRESS, &codec);
	if (ret < 0) {
		return ret;
	}

	for (i = 0U; i < sizeof(mac); i++) {
		mac[i] = (char)rtl8720_get_i8(&codec);
	}

	(void)rtl8720_get_i32(&codec);

	if (codec.err) {
		return -EIO;
	}

	mac[sizeof(mac) - 1] = '\0';

	if (net_bytes_from_str(data->mac, sizeof(data->mac), mac) < 0) {
		LOG_ERR("Module reported an unparsable MAC address");
		return -EIO;
	}

	return 0;
}

static int rtl8720_read_ip_info(struct rtl8720_data *data)
{
	uint32_t iface = RTL8720_TCPIP_IF_STA;
	struct rtl8720_codec codec;
	const uint8_t *info;
	size_t len;
	int ret;

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_u32(&codec, iface);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_TCPIP, RTL8720_TCPIP_GET_IP_INFO, &codec);
	if (ret < 0) {
		return ret;
	}

	info = rtl8720_get_bin(&codec, &len);
	if (codec.err || len < RTL8720_IP_INFO_SIZE) {
		return -EIO;
	}

	/* Three ip4_addr_t, each already in network byte order */
	memcpy(&data->ip.s_addr, &info[0], 4);
	memcpy(&data->nm.s_addr, &info[4], 4);
	memcpy(&data->gw.s_addr, &info[8], 4);

	(void)rtl8720_get_i32(&codec);

	return codec.err ? -EIO : 0;
}

static uint32_t rtl8720_security_to_rtw(enum wifi_security_type security)
{
	switch (security) {
	case WIFI_SECURITY_TYPE_NONE:
		return RTL8720_SECURITY_OPEN;
	case WIFI_SECURITY_TYPE_SAE:
	case WIFI_SECURITY_TYPE_SAE_H2E:
	case WIFI_SECURITY_TYPE_SAE_AUTO:
		return RTL8720_SECURITY_WPA3_AES_PSK;
	default:
		return RTL8720_SECURITY_WPA2_AES_PSK;
	}
}

static enum wifi_security_type rtl8720_security_from_rtw(uint32_t security)
{
	if (security == RTL8720_SECURITY_UNKNOWN) {
		return WIFI_SECURITY_TYPE_UNKNOWN;
	}

	if ((security & RTL8720_SEC_WPA3) != 0U) {
		return WIFI_SECURITY_TYPE_SAE;
	}

	if ((security & RTL8720_SEC_WPA2) != 0U) {
		return WIFI_SECURITY_TYPE_PSK;
	}

	if ((security & RTL8720_SEC_WPA) != 0U) {
		return WIFI_SECURITY_TYPE_WPA_PSK;
	}

	if ((security & RTL8720_SEC_WEP_ENABLED) != 0U) {
		return WIFI_SECURITY_TYPE_WEP;
	}

	return WIFI_SECURITY_TYPE_NONE;
}

static void rtl8720_report_scan_result(struct rtl8720_data *data, const uint8_t *record)
{
	struct wifi_scan_result res = {0};
	uint8_t ssid_len = record[RTL8720_SCAN_OFF_SSID_LEN];

	if (ssid_len > WIFI_SSID_MAX_LEN) {
		ssid_len = WIFI_SSID_MAX_LEN;
	}

	res.ssid_length = ssid_len;
	memcpy(res.ssid, &record[RTL8720_SCAN_OFF_SSID], ssid_len);

	res.mac_length = WIFI_MAC_ADDR_LEN;
	memcpy(res.mac, &record[RTL8720_SCAN_OFF_BSSID], WIFI_MAC_ADDR_LEN);

	res.rssi = (int16_t)sys_get_le16(&record[RTL8720_SCAN_OFF_RSSI]);
	res.security = rtl8720_security_from_rtw(sys_get_le32(&record[RTL8720_SCAN_OFF_SECURITY]));
	res.channel = (uint8_t)sys_get_le32(&record[RTL8720_SCAN_OFF_CHANNEL]);
	res.band = wifi_utils_chan_to_band(res.channel);

	data->scan_cb(data->iface, 0, &res);
}

static int rtl8720_scan_collect(struct rtl8720_data *data, uint16_t count)
{
	struct rtl8720_codec codec;
	const uint8_t *records;
	size_t len;
	size_t i;
	int ret;

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_u16(&codec, count);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_WIFI_DRV, RTL8720_WIFI_SCAN_GET_AP_RECORDS,
				&codec);
	if (ret < 0) {
		return ret;
	}

	records = rtl8720_get_bin(&codec, &len);
	if (codec.err) {
		return -EIO;
	}

	for (i = 0U; (i + RTL8720_SCAN_RESULT_SIZE) <= len; i += RTL8720_SCAN_RESULT_SIZE) {
		rtl8720_report_scan_result(data, &records[i]);
	}

	return 0;
}

static void rtl8720_scan_work(struct k_work *work)
{
	struct rtl8720_data *data = CONTAINER_OF(work, struct rtl8720_data, scan_work);
	struct rtl8720_codec codec;
	uint16_t max_records;
	int32_t result;
	uint32_t count;
	int retries;
	int ret;

	ret = rtl8720_call_i32(data, RTL8720_SVC_WIFI_DRV, RTL8720_WIFI_SCAN_START, NULL, 0U,
			       &result);
	if (ret < 0 || result != RTL8720_RTW_SUCCESS) {
		LOG_ERR("Failed to start a scan: %d", ret);
		goto done;
	}

	/* The firmware has no scan completion this driver can subscribe to */
	for (retries = 0; retries < CONFIG_WIFI_RTL8720_SCAN_POLL_RETRIES; retries++) {
		bool scanning;

		k_sleep(K_MSEC(CONFIG_WIFI_RTL8720_SCAN_POLL_INTERVAL));

		rtl8720_erpc_request(data, &codec);

		ret = rtl8720_erpc_call(data, RTL8720_SVC_WIFI_DRV, RTL8720_WIFI_IS_SCANNING,
					&codec);
		if (ret < 0) {
			goto done;
		}

		scanning = rtl8720_get_i8(&codec) != 0;
		if (codec.err) {
			ret = -EIO;
			goto done;
		}

		if (!scanning) {
			break;
		}
	}

	rtl8720_erpc_request(data, &codec);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_WIFI_DRV, RTL8720_WIFI_SCAN_GET_AP_NUM, &codec);
	if (ret < 0) {
		goto done;
	}

	count = rtl8720_get_u16(&codec);
	if (codec.err) {
		ret = -EIO;
		goto done;
	}

	/*
	 * Records come back in one blob, so ask for no more of them than the
	 * receive buffer can hold.
	 */
	max_records =
		(uint16_t)((CONFIG_WIFI_RTL8720_RX_BUF_SIZE - 16U) / RTL8720_SCAN_RESULT_SIZE);
	if (count > max_records) {
		LOG_WRN("Reporting %u of %u access points", max_records, count);
		count = max_records;
	}

	if (count > 0U) {
		ret = rtl8720_scan_collect(data, (uint16_t)count);
	}

done:
	data->scan_cb(data->iface, ret, NULL);
	data->scan_cb = NULL;
}

static int rtl8720_mgmt_scan(const struct device *dev, struct net_if *iface,
			     struct wifi_scan_params *params, scan_result_cb_t cb)
{
	struct rtl8720_data *data = dev->data;

	ARG_UNUSED(iface);
	ARG_UNUSED(params);

	if (data->scan_cb != NULL) {
		return -EINPROGRESS;
	}

	data->scan_cb = cb;
	k_work_submit_to_queue(&data->workq, &data->scan_work);

	return 0;
}

static void rtl8720_connect_work(struct k_work *work)
{
	struct rtl8720_data *data = CONTAINER_OF(work, struct rtl8720_data, connect_work);
	struct rtl8720_codec codec;
	const char *psk = NULL;
	int32_t result;
	int ret;

	if (data->connect_params.security != WIFI_SECURITY_TYPE_NONE) {
		psk = data->connect_psk;
	}

	rtl8720_erpc_request(data, &codec);
	rtl8720_put_str(&codec, data->connect_ssid);
	rtl8720_put_nullable_str(&codec, psk);
	rtl8720_put_u32(&codec, rtl8720_security_to_rtw(data->connect_params.security));
	rtl8720_put_i32(&codec, -1);
	rtl8720_put_u32(&codec, 0U);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_WIFI_DRV, RTL8720_WIFI_CONNECT, &codec);
	if (ret < 0) {
		goto out;
	}

	result = rtl8720_get_i32(&codec);
	if (codec.err) {
		ret = -EIO;
		goto out;
	}

	if (result != RTL8720_RTW_SUCCESS) {
		LOG_WRN("Association refused: %d", result);
		ret = -EIO;
		goto out;
	}

	ret = rtl8720_tcpip_dhcpc_start(data);
	if (ret < 0) {
		LOG_ERR("DHCP client did not start: %d", ret);
		goto out;
	}

	ret = rtl8720_read_ip_info(data);
	if (ret < 0) {
		LOG_WRN("Could not read the leased address: %d", ret);
		ret = 0;
	}

out:
	if (ret == 0) {
		data->connected = true;
		net_if_dormant_off(data->iface);

		if (IS_ENABLED(CONFIG_NET_NATIVE_IPV4)) {
			net_if_ipv4_addr_add(data->iface, &data->ip, NET_ADDR_DHCP, 0);
			net_if_ipv4_set_netmask_by_addr(data->iface, &data->ip, &data->nm);
			net_if_ipv4_set_gw(data->iface, &data->gw);
		}
	}

	wifi_mgmt_raise_connect_result_event(data->iface, ret);
}

static int rtl8720_mgmt_connect(const struct device *dev, struct net_if *iface,
				struct wifi_connect_req_params *params)
{
	struct rtl8720_data *data = dev->data;

	ARG_UNUSED(iface);

	if (params->ssid_length > WIFI_SSID_MAX_LEN || params->ssid_length == 0U) {
		return -EINVAL;
	}

	if (params->psk_length > WIFI_PSK_MAX_LEN) {
		return -EINVAL;
	}

	if (data->connected) {
		return -EALREADY;
	}

	memcpy(data->connect_ssid, params->ssid, params->ssid_length);
	data->connect_ssid[params->ssid_length] = '\0';

	if (params->psk_length > 0U) {
		memcpy(data->connect_psk, params->psk, params->psk_length);
	}
	data->connect_psk[params->psk_length] = '\0';

	data->connect_params = *params;

	k_work_submit_to_queue(&data->workq, &data->connect_work);

	return 0;
}

static void rtl8720_disconnect_work(struct k_work *work)
{
	struct rtl8720_data *data = CONTAINER_OF(work, struct rtl8720_data, disconnect_work);
	int32_t result;
	int ret;

	ret = rtl8720_call_i32(data, RTL8720_SVC_WIFI_DRV, RTL8720_WIFI_DISCONNECT, NULL, 0U,
			       &result);
	if (ret == 0 && result != RTL8720_RTW_SUCCESS) {
		ret = -EIO;
	}

	if (ret == 0) {
		data->connected = false;
		net_if_dormant_on(data->iface);

		if (IS_ENABLED(CONFIG_NET_NATIVE_IPV4)) {
			net_if_ipv4_addr_rm(data->iface, &data->ip);
		}
	}

	wifi_mgmt_raise_disconnect_result_event(data->iface, ret);
}

static int rtl8720_mgmt_disconnect(const struct device *dev, struct net_if *iface)
{
	struct rtl8720_data *data = dev->data;

	ARG_UNUSED(iface);

	if (!data->connected) {
		return -EALREADY;
	}

	k_work_submit_to_queue(&data->workq, &data->disconnect_work);

	return 0;
}

static int rtl8720_mgmt_iface_status(const struct device *dev, struct net_if *iface,
				     struct wifi_iface_status *status)
{
	struct rtl8720_data *data = dev->data;
	struct rtl8720_codec codec;
	int32_t rssi;
	int ret;

	ARG_UNUSED(iface);

	status->iface_mode = WIFI_MODE_INFRA;
	status->link_mode = WIFI_LINK_MODE_UNKNOWN;
	status->state = data->connected ? WIFI_STATE_COMPLETED : WIFI_STATE_DISCONNECTED;

	if (!data->connected) {
		return 0;
	}

	status->ssid_len = strlen(data->connect_ssid);
	memcpy(status->ssid, data->connect_ssid, status->ssid_len);
	status->security = data->connect_params.security;

	rtl8720_erpc_request(data, &codec);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_WIFI_DRV, RTL8720_WIFI_GET_RSSI, &codec);
	if (ret == 0) {
		rssi = rtl8720_get_i32(&codec);
		if (!codec.err) {
			status->rssi = rssi;
		}
	}

	rtl8720_erpc_request(data, &codec);

	ret = rtl8720_erpc_call(data, RTL8720_SVC_WIFI_DRV, RTL8720_WIFI_GET_CHANNEL, &codec);
	if (ret == 0) {
		int32_t channel = rtl8720_get_i32(&codec);

		if (!codec.err) {
			status->channel = (uint8_t)channel;
			status->band = wifi_utils_chan_to_band(status->channel);
		}
	}

	return 0;
}

static void rtl8720_iface_init(struct net_if *iface)
{
	struct rtl8720_data *data = &rtl8720_driver_data;

	data->iface = iface;

	net_if_set_link_addr(iface, data->mac, sizeof(data->mac), NET_LINK_ETHERNET);

	if (IS_ENABLED(CONFIG_NET_SOCKETS_OFFLOAD)) {
		rtl8720_socket_offload_init(iface);
	}

	/* Associated only once the application asks for it */
	net_if_dormant_on(iface);
}

static enum offloaded_net_if_types rtl8720_offload_get_type(void)
{
	return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}

static const struct wifi_mgmt_ops rtl8720_mgmt_ops = {
	.scan = rtl8720_mgmt_scan,
	.connect = rtl8720_mgmt_connect,
	.disconnect = rtl8720_mgmt_disconnect,
	.iface_status = rtl8720_mgmt_iface_status,
};

static const struct net_wifi_mgmt_offload rtl8720_api = {
	.wifi_iface.iface_api.init = rtl8720_iface_init,
	.wifi_iface.get_type = rtl8720_offload_get_type,
	.wifi_mgmt_api = &rtl8720_mgmt_ops,
};

static int rtl8720_init(const struct device *dev);

NET_DEVICE_DT_INST_OFFLOAD_DEFINE(0, rtl8720_init, NULL, &rtl8720_driver_data, NULL,
				  CONFIG_WIFI_INIT_PRIORITY, &rtl8720_api, CONFIG_WIFI_RTL8720_MTU);

CONNECTIVITY_WIFI_MGMT_BIND(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)));

static int rtl8720_init(const struct device *dev)
{
	struct rtl8720_data *data = dev->data;
	int ret;

	k_mutex_init(&data->lock);
	k_sem_init(&data->reply, 0, 1);
	k_sem_init(&data->rx_sem, 0, 1);

	k_work_init(&data->scan_work, rtl8720_scan_work);
	k_work_init(&data->connect_work, rtl8720_connect_work);
	k_work_init(&data->disconnect_work, rtl8720_disconnect_work);

	k_work_queue_start(&data->workq, rtl8720_workq_stack,
			   K_KERNEL_STACK_SIZEOF(rtl8720_workq_stack),
			   K_PRIO_COOP(CONFIG_WIFI_RTL8720_WORKQ_THREAD_PRIORITY), NULL);
	k_thread_name_set(data->workq.thread_id, "rtl8720_workq");

	ret = rtl8720_reset();
	if (ret < 0) {
		return ret;
	}

	ret = rtl8720_erpc_init(dev);
	if (ret < 0) {
		return ret;
	}

	ret = rtl8720_wifi_on(data, RTL8720_MODE_STA);
	if (ret < 0) {
		LOG_ERR("Could not bring the radio up: %d", ret);
		return ret;
	}

	ret = rtl8720_tcpip_init(data);
	if (ret < 0) {
		LOG_ERR("Could not start the TCP/IP adapter: %d", ret);
		return ret;
	}

	ret = rtl8720_read_mac(data);
	if (ret < 0) {
		LOG_ERR("Could not read the MAC address: %d", ret);
		return ret;
	}

	LOG_INF("RTL8720 ready, MAC %02x:%02x:%02x:%02x:%02x:%02x", data->mac[0], data->mac[1],
		data->mac[2], data->mac[3], data->mac[4], data->mac[5]);

	return 0;
}
