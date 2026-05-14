/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <zephyr/meshtastic/meshtastic.h>
#include <zephyr/meshtastic/nodedb.h>
#include <zephyr/meshtastic/nodeinfo.h>
#include <zephyr/meshtastic/telemetry.h>

#include <pb_encode.h>

#if defined(CONFIG_MESHTASTIC_PHONEAPI)
#include <pb_decode.h>

#include "meshtastic_phoneapi.h"
#endif

#if defined(CONFIG_MESHTASTIC_SETTINGS)
#include "meshtastic_channels.h"
#include "meshtastic_config_store.h"
#include "meshtastic_core.h"
#endif

#if defined(CONFIG_MESHTASTIC_SERIAL)
#include "meshtastic_serial.h"
#endif

#include "meshtastic_packet.h"
#include "meshtastic_router.h"

struct fake_lora_data {
	struct lora_modem_config last_config;
	struct lora_modem_config send_config;
	uint8_t last_tx[255];
	uint32_t last_tx_len;
	uint32_t send_count;
	lora_recv_cb rx_cb;
	void *rx_user_data;
};

static struct fake_lora_data fake_data;

static int fake_lora_config(const struct device *dev,
			    const struct lora_modem_config *config)
{
	struct fake_lora_data *data = dev->data;

	data->last_config = *config;
	return 0;
}

static uint32_t fake_lora_airtime(const struct device *dev, uint32_t data_len)
{
	ARG_UNUSED(dev);

	return data_len;
}

static int fake_lora_send(const struct device *dev, uint8_t *data,
			  uint32_t data_len)
{
	struct fake_lora_data *fake = dev->data;

	zassert_true(data_len <= sizeof(fake->last_tx));
	memcpy(fake->last_tx, data, data_len);
	fake->last_tx_len = data_len;
	fake->send_config = fake->last_config;
	fake->send_count++;

	return 0;
}

static int fake_lora_send_async(const struct device *dev, uint8_t *data,
				uint32_t data_len,
				struct k_poll_signal *async)
{
	int ret;

	ret = fake_lora_send(dev, data, data_len);
	if (async != NULL) {
		k_poll_signal_raise(async, ret);
	}

	return ret;
}

static int fake_lora_recv(const struct device *dev, uint8_t *data,
			  uint8_t size, k_timeout_t timeout,
			  int16_t *rssi, int8_t *snr)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(data);
	ARG_UNUSED(size);
	ARG_UNUSED(rssi);
	ARG_UNUSED(snr);

	k_sleep(timeout);
	return 0;
}

static int fake_lora_recv_async(const struct device *dev, lora_recv_cb cb,
				void *user_data)
{
	struct fake_lora_data *fake = dev->data;

	fake->rx_cb = cb;
	fake->rx_user_data = user_data;

	return 0;
}

static int fake_lora_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static DEVICE_API(lora, fake_lora_api) = {
	.config = fake_lora_config,
	.airtime = fake_lora_airtime,
	.send = fake_lora_send,
	.send_async = fake_lora_send_async,
	.recv = fake_lora_recv,
	.recv_async = fake_lora_recv_async,
};

DEVICE_DEFINE(fake_lora, "fake_lora", fake_lora_init, NULL, &fake_data, NULL,
	      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &fake_lora_api);

#define DT_DRV_COMPAT vnd_env_sensor

#if DT_HAS_COMPAT_STATUS_OKAY(vnd_env_sensor)
#include <zephyr/drivers/sensor.h>

static int env_sensor_fetch(const struct device *dev, enum sensor_channel chan)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan);

	return 0;
}

static int env_sensor_get(const struct device *dev, enum sensor_channel chan,
			  struct sensor_value *val)
{
	ARG_UNUSED(dev);

	switch (chan) {
	case SENSOR_CHAN_AMBIENT_TEMP: /* 23.5 C */
		val->val1 = 23;
		val->val2 = 500000;
		break;
	case SENSOR_CHAN_HUMIDITY: /* 45 %RH */
		val->val1 = 45;
		val->val2 = 0;
		break;
	case SENSOR_CHAN_PRESS: /* 101.3 kPa -> 1013 hPa */
		val->val1 = 101;
		val->val2 = 300000;
		break;
	case SENSOR_CHAN_GAS_RES: /* 2_000_000 ohm -> 2 MOhm */
		val->val1 = 2000000;
		val->val2 = 0;
		break;
	case SENSOR_CHAN_LIGHT: /* 250 lux */
		val->val1 = 250;
		val->val2 = 0;
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static DEVICE_API(sensor, env_sensor_api) = {
	.sample_fetch = env_sensor_fetch,
	.channel_get = env_sensor_get,
};

static int env_sensor_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

DEVICE_DT_INST_DEFINE(0, env_sensor_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_SENSOR_INIT_PRIORITY, &env_sensor_api);
#endif /* DT_HAS_COMPAT_STATUS_OKAY(vnd_env_sensor) */

static const struct device *fake_lora_dev = DEVICE_GET(fake_lora);
static bool stack_initialized;

static uint32_t last_tx_dest(void)
{
	return sys_get_le32(fake_data.last_tx);
}

struct inject_capture_data {
	bool seen;
	uint32_t from;
	uint32_t to;
	uint32_t portnum;
	uint8_t payload[MESHTASTIC_MAX_PAYLOAD_LEN];
	size_t payload_len;
};

static struct inject_capture_data inject_capture;

static void capture_packet_event(const struct meshtastic_event *event, void *user_data)
{
	struct inject_capture_data *capture = user_data;
	const struct meshtastic_packet *packet;

	if (event->type != MESHTASTIC_EVENT_PACKET_RECEIVED || event->packet == NULL) {
		return;
	}

	packet = event->packet;
	capture->seen = true;
	capture->from = packet->from;
	capture->to = packet->to;
	capture->portnum = packet->portnum;
	capture->payload_len = packet->payload_len;
	if (packet->payload_len > 0U) {
		zassert_not_null(packet->payload);
		memcpy(capture->payload, packet->payload, packet->payload_len);
	}
}

static void capture_reset(void)
{
	memset(&inject_capture, 0, sizeof(inject_capture));
	meshtastic_set_event_cb(capture_packet_event, &inject_capture);
}

static void capture_stop(void)
{
	meshtastic_set_event_cb(NULL, NULL);
}

static uint32_t test_mesh_packet_id = 0xabc100U;

static size_t encode_pb(const pb_msgdesc_t *fields, const void *src, uint8_t *buf,
			size_t buf_len)
{
	pb_ostream_t stream = pb_ostream_from_buffer(buf, buf_len);

	zassert_true(pb_encode(&stream, fields, src), "protobuf encode failed: %s",
		     PB_GET_ERROR(&stream));
	return stream.bytes_written;
}

static void inject_decoded_packet(uint32_t from, uint32_t portnum, const uint8_t *payload,
				  size_t payload_len)
{
	meshtastic_MeshPacket mesh = meshtastic_MeshPacket_init_zero;

	zassert_true(payload_len <= sizeof(mesh.decoded.payload.bytes));

	mesh.from = from;
	mesh.to = meshtastic_get_node_id();
	mesh.id = test_mesh_packet_id++;
	mesh.hop_limit = 2U;
	mesh.hop_start = 3U;
	mesh.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
	mesh.decoded.portnum = (meshtastic_PortNum)portnum;
	mesh.decoded.payload.size = payload_len;
	if (payload_len > 0U) {
		memcpy(mesh.decoded.payload.bytes, payload, payload_len);
	}

	zassert_ok(meshtastic_inject_downlink_mesh_packet(&mesh));
}

static void *meshtastic_setup(void)
{
	struct meshtastic_config cfg = {
		.lora_dev = fake_lora_dev,
		.node_id = 0x12345678,
		.psk = meshtastic_default_psk,
		.psk_len = sizeof(meshtastic_default_psk),
		.channel_name = MESHTASTIC_CHANNEL_LONGFAST,
		.frequency = MESHTASTIC_FREQ_EU,
		.long_name = "Zephyr Test",
		.short_name = "ZT",
	};

	if (!stack_initialized) {
		zassert_ok(meshtastic_init(&cfg));
		stack_initialized = true;
	}
	return NULL;
}

ZTEST(meshtastic_core, test_status_after_init)
{
	struct meshtastic_status status;

	zassert_ok(meshtastic_get_status(&status));
	zassert_true(status.initialized);
	zassert_equal(status.node_id, 0x12345678);
}

ZTEST(meshtastic_core, test_nodedb_local_node_after_init)
{
	struct meshtastic_nodedb_node node;

	zassert_true(meshtastic_nodedb_count() >= 1U);
	zassert_ok(meshtastic_nodedb_get(meshtastic_get_node_id(), &node));
	zassert_equal(node.num, meshtastic_get_node_id());
	zassert_true(node.has_user);
	zassert_str_equal(node.long_name, "Zephyr Test");
	zassert_str_equal(node.short_name, "ZT");
}

ZTEST(meshtastic_core, test_send_text)
{
	const uint32_t dest = 0x87654321U;
	uint32_t before = fake_data.send_count;

	zassert_ok(meshtastic_send_text(dest, "hello"));
	zassert_equal(fake_data.send_count, before + 1U);
	zassert_true(fake_data.last_tx_len > 16U);
	zassert_equal(last_tx_dest(), dest);
	zassert_true(fake_data.send_config.tx);
}

ZTEST(meshtastic_core, test_send_raw_data_destination)
{
	const uint32_t dest = 0x01020304U;
	const uint8_t payload[] = "raw";
	uint32_t before = fake_data.send_count;

	zassert_ok(meshtastic_send_data(dest, MESHTASTIC_PORT_PRIVATE, payload, sizeof(payload) - 1U));
	zassert_equal(fake_data.send_count, before + 1U);
	zassert_true(fake_data.last_tx_len > 16U);
	zassert_equal(last_tx_dest(), dest);
}

ZTEST(meshtastic_core, test_nodedb_nodeinfo_packet_updates_remote)
{
	const uint32_t remote = 0x20001001U;
	meshtastic_User user = meshtastic_User_init_zero;
	struct meshtastic_nodedb_node node;
	uint8_t payload[MESHTASTIC_MAX_PAYLOAD_LEN];
	size_t len;

	strncpy(user.long_name, "Remote Node", sizeof(user.long_name) - 1U);
	strncpy(user.short_name, "RN", sizeof(user.short_name) - 1U);
	user.hw_model = meshtastic_HardwareModel_TBEAM;
	user.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
	user.is_licensed = true;
	len = encode_pb(meshtastic_User_fields, &user, payload, sizeof(payload));

	inject_decoded_packet(remote, MESHTASTIC_PORT_NODEINFO, payload, len);

	zassert_ok(meshtastic_nodedb_get(remote, &node));
	zassert_true(node.has_user);
	zassert_str_equal(node.long_name, "Remote Node");
	zassert_str_equal(node.short_name, "RN");
	zassert_equal(node.hw_model, meshtastic_HardwareModel_TBEAM);
	zassert_true(node.is_licensed);
	zassert_true(node.via_mqtt);
	zassert_true(node.has_hops_away);
	zassert_equal(node.hops_away, 1U);
}

ZTEST(meshtastic_core, test_nodedb_position_and_telemetry_update_remote)
{
	const uint32_t remote = 0x20001002U;
	meshtastic_Position position = meshtastic_Position_init_zero;
	meshtastic_Telemetry telemetry = meshtastic_Telemetry_init_zero;
	struct meshtastic_nodedb_node node;
	uint8_t payload[MESHTASTIC_MAX_PAYLOAD_LEN];
	size_t len;

	position.has_latitude_i = true;
	position.latitude_i = 481234567;
	position.has_longitude_i = true;
	position.longitude_i = 23456789;
	position.has_altitude = true;
	position.altitude = 123;
	position.time = 42U;
	position.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;
	position.precision_bits = 32U;
	len = encode_pb(meshtastic_Position_fields, &position, payload, sizeof(payload));
	inject_decoded_packet(remote, MESHTASTIC_PORT_POSITION, payload, len);

	telemetry.which_variant = meshtastic_Telemetry_device_metrics_tag;
	telemetry.variant.device_metrics.has_battery_level = true;
	telemetry.variant.device_metrics.battery_level = 87U;
	telemetry.variant.device_metrics.has_voltage = true;
	telemetry.variant.device_metrics.voltage = 4.125f;
	len = encode_pb(meshtastic_Telemetry_fields, &telemetry, payload, sizeof(payload));
	inject_decoded_packet(remote, MESHTASTIC_PORT_TELEMETRY, payload, len);

	zassert_ok(meshtastic_nodedb_get(remote, &node));
	zassert_true(node.has_position);
	zassert_equal(node.position.latitude_i, position.latitude_i);
	zassert_equal(node.position.longitude_i, position.longitude_i);
	zassert_equal(node.position.altitude, position.altitude);
	zassert_equal(node.position.time, position.time);
	zassert_true(node.has_device_metrics);
	zassert_true(node.device_metrics.has_battery_level);
	zassert_equal(node.device_metrics.battery_level, 87U);
	zassert_true(node.device_metrics.has_voltage);
	zassert_within((int)(node.device_metrics.voltage * 1000.0f), 4125, 1);
}

ZTEST(meshtastic_core, test_nodedb_eviction_keeps_local_node)
{
	struct meshtastic_nodedb_node node;

	for (uint32_t i = 0U; i < CONFIG_MESHTASTIC_NODEDB_MAX_NODES + 3U; i++) {
		inject_decoded_packet(0x30000000U + i, MESHTASTIC_PORT_PRIVATE, NULL, 0U);
		k_sleep(K_MSEC(1));
	}

	zassert_equal(meshtastic_nodedb_count(), CONFIG_MESHTASTIC_NODEDB_MAX_NODES);
	zassert_ok(meshtastic_nodedb_get(meshtastic_get_node_id(), &node));
	zassert_equal(node.num, meshtastic_get_node_id());
}

ZTEST(meshtastic_core, test_nodedb_shell_commands)
{
	const struct shell *sh = shell_backend_dummy_get_ptr();

	shell_backend_dummy_clear_output(sh);
	zassert_ok(shell_execute_cmd(sh, "meshtastic nodedb list"));
	shell_backend_dummy_clear_output(sh);
	zassert_ok(shell_execute_cmd(sh, "meshtastic nodedb show 0x12345678"));
}

ZTEST(meshtastic_core, test_reject_oversized_payload)
{
	uint8_t payload[MESHTASTIC_MAX_PAYLOAD_LEN + 1U] = { 0 };

	zassert_equal(meshtastic_send_data(MESHTASTIC_NODE_BROADCAST,
					   MESHTASTIC_PORT_PRIVATE,
					   payload, sizeof(payload)),
		      -EINVAL);
}

ZTEST(meshtastic_core, test_send_uptime_metrics)
{
	const uint32_t dest = 0x11112222U;
	uint32_t before = fake_data.send_count;

	zassert_ok(meshtastic_send_device_metrics(dest));
	zassert_equal(fake_data.send_count, before + 1U);
	zassert_true(fake_data.last_tx_len > 16U);
	zassert_equal(last_tx_dest(), dest);
}

ZTEST(meshtastic_core, test_send_node_info)
{
	const uint32_t dest = 0x33334444U;
	uint32_t before = fake_data.send_count;

	zassert_ok(meshtastic_send_node_info(dest));
	zassert_equal(fake_data.send_count, before + 1U);
	zassert_true(fake_data.last_tx_len > 16U);
	zassert_equal(last_tx_dest(), dest);
}

ZTEST(meshtastic_core, test_inject_decoded_downlink_packet)
{
	const uint8_t payload[] = "downlink";
	meshtastic_MeshPacket mesh = meshtastic_MeshPacket_init_zero;

	mesh.from = 0x20000001U;
	mesh.to = meshtastic_get_node_id();
	mesh.id = 0xabc001U;
	mesh.hop_limit = 3U;
	mesh.hop_start = 3U;
	mesh.channel = 0U;
	mesh.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
	mesh.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
	mesh.decoded.payload.size = sizeof(payload) - 1U;
	memcpy(mesh.decoded.payload.bytes, payload, sizeof(payload) - 1U);

	capture_reset();
	zassert_ok(meshtastic_inject_downlink_mesh_packet(&mesh));
	capture_stop();

	zassert_true(inject_capture.seen);
	zassert_equal(inject_capture.from, mesh.from);
	zassert_equal(inject_capture.to, mesh.to);
	zassert_equal(inject_capture.portnum, MESHTASTIC_PORT_TEXT_MESSAGE);
	zassert_equal(inject_capture.payload_len, sizeof(payload) - 1U);
	zassert_mem_equal(inject_capture.payload, payload, sizeof(payload) - 1U);
}

ZTEST(meshtastic_core, test_inject_encrypted_downlink_packet)
{
	const uint8_t payload[] = "encrypted-downlink";
	const uint32_t from = 0x20000002U;
	const uint32_t id = 0xabc002U;
	struct meshtastic_packet packet = {
		.from = from,
		.to = meshtastic_get_node_id(),
		.id = id,
		.portnum = MESHTASTIC_PORT_TEXT_MESSAGE,
		.payload = payload,
		.payload_len = sizeof(payload) - 1U,
	};
	const struct meshtastic_wire_header *hdr;
	meshtastic_MeshPacket mesh = meshtastic_MeshPacket_init_zero;
	uint32_t before = fake_data.send_count;

	zassert_ok(meshtastic_send_packet(&packet));
	zassert_equal(fake_data.send_count, before + 1U);
	zassert_true(fake_data.last_tx_len > MESHTASTIC_HDR_LEN);

	hdr = (const struct meshtastic_wire_header *)fake_data.last_tx;
	mesh.from = sys_le32_to_cpu(hdr->src);
	mesh.to = sys_le32_to_cpu(hdr->dest);
	mesh.id = sys_le32_to_cpu(hdr->id);
	mesh.hop_limit = hdr->flags & MESHTASTIC_FLAGS_HOP_LIMIT_MASK;
	mesh.hop_start = (hdr->flags & MESHTASTIC_FLAGS_HOP_START_MASK) >>
			 MESHTASTIC_FLAGS_HOP_START_SHIFT;
	mesh.channel = hdr->channel;
	mesh.next_hop = hdr->next_hop;
	mesh.relay_node = hdr->relay_node;
	mesh.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
	mesh.encrypted.size = fake_data.last_tx_len - MESHTASTIC_HDR_LEN;
	memcpy(mesh.encrypted.bytes, fake_data.last_tx + MESHTASTIC_HDR_LEN, mesh.encrypted.size);

	capture_reset();
	zassert_ok(meshtastic_inject_downlink_mesh_packet(&mesh));
	capture_stop();

	zassert_true(inject_capture.seen);
	zassert_equal(inject_capture.from, from);
	zassert_equal(inject_capture.to, meshtastic_get_node_id());
	zassert_equal(inject_capture.portnum, MESHTASTIC_PORT_TEXT_MESSAGE);
	zassert_equal(inject_capture.payload_len, sizeof(payload) - 1U);
	zassert_mem_equal(inject_capture.payload, payload, sizeof(payload) - 1U);
}

#if defined(CONFIG_MESHTASTIC_ENVIRONMENT_METRICS)
ZTEST(meshtastic_core, test_send_environment)
{
	const uint32_t dest = 0x55556666U;
	uint32_t before = fake_data.send_count;
	int ret = meshtastic_send_environment(dest);

#if DT_HAS_COMPAT_STATUS_OKAY(vnd_env_sensor)
	zassert_ok(ret);
	zassert_equal(fake_data.send_count, before + 1U);
	zassert_true(fake_data.last_tx_len > 16U);
	zassert_equal(last_tx_dest(), dest);
#else
	zassert_equal(ret, -ENODEV);
	zassert_equal(fake_data.send_count, before);
#endif
}
#endif

#if defined(CONFIG_MESHTASTIC_SERIAL)
static int feed_serial_bytes(const uint8_t *bytes, size_t len, uint8_t *payload,
			     size_t *frame_len)
{
	int ret = 0;

	for (size_t i = 0; i < len; i++) {
		ret = meshtastic_serial_decode_byte(bytes[i], payload, MESHTASTIC_API_FRAME_MAX,
						    frame_len);
		if (ret != 0) {
			return ret;
		}
	}

	return ret;
}

ZTEST(meshtastic_core, test_serial_stream_framing)
{
	const uint8_t payload1[] = { 0x01, 0x02, 0x03 };
	const uint8_t payload2[] = { 0x04, 0x05 };
	uint8_t frame[16];
	uint8_t decoded[MESHTASTIC_API_FRAME_MAX];
	size_t frame_len;
	size_t encoded_len;
	int ret;

	encoded_len = meshtastic_serial_encode_frame(payload1, sizeof(payload1), frame,
						     sizeof(frame));
	zassert_equal(encoded_len, sizeof(payload1) + 4U);
	zassert_equal(frame[0], 0x94);
	zassert_equal(frame[1], 0xc3);
	zassert_equal(frame[2], 0x00);
	zassert_equal(frame[3], sizeof(payload1));
	zassert_mem_equal(&frame[4], payload1, sizeof(payload1));

	const uint8_t garbage[] = { 0x00, 0xff, 0x94, 0x00 };

	frame_len = 0U;
	zassert_equal(feed_serial_bytes(garbage, sizeof(garbage), decoded, &frame_len), 0);
	zassert_equal(frame_len, 0U);

	frame_len = 0U;
	zassert_equal(feed_serial_bytes(frame, 2U, decoded, &frame_len), 0);
	zassert_equal(feed_serial_bytes(&frame[2], encoded_len - 2U, decoded, &frame_len), 1);
	zassert_equal(frame_len, sizeof(payload1));
	zassert_mem_equal(decoded, payload1, sizeof(payload1));

	encoded_len = meshtastic_serial_encode_frame(payload2, sizeof(payload2), frame,
						     sizeof(frame));
	frame_len = 0U;
	zassert_equal(feed_serial_bytes(frame, encoded_len, decoded, &frame_len), 1);
	zassert_equal(frame_len, sizeof(payload2));
	zassert_mem_equal(decoded, payload2, sizeof(payload2));

	const uint8_t oversized[] = {
		0x94, 0xc3, (uint8_t)((MESHTASTIC_API_FRAME_MAX + 1U) >> 8),
		(uint8_t)(MESHTASTIC_API_FRAME_MAX + 1U),
	};

	frame_len = 0U;
	ret = feed_serial_bytes(oversized, sizeof(oversized), decoded, &frame_len);
	zassert_equal(ret, -EMSGSIZE);
	zassert_equal(frame_len, 0U);

	encoded_len = meshtastic_serial_encode_frame(payload1, sizeof(payload1), frame,
						     sizeof(frame));
	frame_len = 0U;
	zassert_equal(feed_serial_bytes(frame, encoded_len, decoded, &frame_len), 1);
	zassert_equal(frame_len, sizeof(payload1));
	zassert_mem_equal(decoded, payload1, sizeof(payload1));

	zassert_equal(meshtastic_serial_encode_frame(payload1, sizeof(payload1), frame, 2U), 0U);
}
#endif

#if defined(CONFIG_MESHTASTIC_PHONEAPI)
static struct meshtastic_phoneapi test_phoneapi;
static struct meshtastic_phoneapi_frame test_phoneapi_queue[40];
static bool test_phoneapi_initialized;

static void test_phoneapi_ensure_initialized(void)
{
	if (!test_phoneapi_initialized) {
		meshtastic_phoneapi_init(&test_phoneapi, "test", test_phoneapi_queue,
					 ARRAY_SIZE(test_phoneapi_queue), NULL, NULL, NULL);
		test_phoneapi_initialized = true;
	}

	meshtastic_phoneapi_reset(&test_phoneapi);
}

static void decode_fromradio_frame(const struct meshtastic_phoneapi_frame *frame,
				   meshtastic_FromRadio *from)
{
	pb_istream_t stream;

	stream = pb_istream_from_buffer(frame->data, frame->len);
	zassert_true(pb_decode(&stream, meshtastic_FromRadio_fields, from),
		     "FromRadio decode failed: %s", PB_GET_ERROR(&stream));
}

static bool test_phoneapi_pop(meshtastic_FromRadio *from)
{
	struct meshtastic_phoneapi_frame frame;

	if (!meshtastic_phoneapi_pop_frame(&test_phoneapi, &frame)) {
		return false;
	}

	decode_fromradio_frame(&frame, from);
	return true;
}

static size_t encode_toradio(const meshtastic_ToRadio *to, uint8_t *buf, size_t buf_len)
{
	pb_ostream_t stream = pb_ostream_from_buffer(buf, buf_len);

	zassert_true(pb_encode(&stream, meshtastic_ToRadio_fields, to),
		     "ToRadio encode failed: %s", PB_GET_ERROR(&stream));
	return stream.bytes_written;
}

ZTEST(meshtastic_core, test_phoneapi_want_config)
{
	meshtastic_ToRadio to = meshtastic_ToRadio_init_zero;
	uint8_t buf[MESHTASTIC_API_FRAME_MAX];
	size_t len;
	bool saw_my_info = false;
	bool saw_config_complete = false;
	bool saw_queue_status = false;

	test_phoneapi_ensure_initialized();

	to.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
	to.want_config_id = 0x1234U;
	len = encode_toradio(&to, buf, sizeof(buf));

	meshtastic_phoneapi_handle_toradio(&test_phoneapi, buf, len);

	while (true) {
		meshtastic_FromRadio from = meshtastic_FromRadio_init_zero;

		if (!test_phoneapi_pop(&from)) {
			break;
		}

		if (from.which_payload_variant == meshtastic_FromRadio_my_info_tag) {
			saw_my_info = true;
		} else if (from.which_payload_variant == meshtastic_FromRadio_config_complete_id_tag) {
			saw_config_complete = (from.config_complete_id == 0x1234U);
		} else if (from.which_payload_variant == meshtastic_FromRadio_queueStatus_tag) {
			saw_queue_status = true;
		}
	}

	zassert_true(saw_my_info);
	zassert_true(saw_queue_status);
	zassert_true(saw_config_complete);
}

#if defined(CONFIG_MESHTASTIC_SETTINGS)
#define TEST_STORE_RECORD_VERSION 1U
#define TEST_OWNER_RECORD_LONG_NAME_OFF 1U
#define TEST_OWNER_RECORD_SHORT_NAME_OFF 41U
#define TEST_OWNER_RECORD_LEN 46U

static size_t encode_config_store_record(const meshtastic_Config *config, uint8_t *buf,
					 size_t buf_len)
{
	pb_ostream_t stream;

	zassert_true(buf_len >= 4U);

	stream = pb_ostream_from_buffer(buf + 4U, buf_len - 4U);
	zassert_true(pb_encode(&stream, meshtastic_Config_fields, config),
		     "Config store record encode failed: %s", PB_GET_ERROR(&stream));

	buf[0] = TEST_STORE_RECORD_VERSION;
	buf[1] = 0U;
	sys_put_le16((uint16_t)stream.bytes_written, buf + 2U);

	return stream.bytes_written + 4U;
}

ZTEST(meshtastic_core, test_settings_store_default_seed)
{
	meshtastic_Channel channel = meshtastic_Channel_init_zero;
	meshtastic_Config config = meshtastic_Config_init_zero;
	meshtastic_ModuleConfig module = meshtastic_ModuleConfig_init_zero;

	zassert_str_equal(meshtastic_config_store_long_name(), "Zephyr Test");
	zassert_str_equal(meshtastic_config_store_short_name(), "ZT");

	zassert_ok(meshtastic_config_store_get_channel(0, &channel));
	zassert_equal(channel.role, meshtastic_Channel_Role_PRIMARY);
	zassert_str_equal(channel.settings.name, MESHTASTIC_CHANNEL_LONGFAST);

	zassert_ok(meshtastic_config_store_get_config(meshtastic_Config_lora_tag, &config));
	zassert_equal(config.payload_variant.lora.hop_limit,
		      CONFIG_MESHTASTIC_DEFAULT_HOP_LIMIT);
	zassert_equal(config.payload_variant.lora.region,
		      meshtastic_Config_LoRaConfig_RegionCode_EU_868);

	zassert_ok(meshtastic_config_store_get_module(meshtastic_ModuleConfig_statusmessage_tag,
						      &module));
	zassert_equal(module.which_payload_variant, meshtastic_ModuleConfig_statusmessage_tag);
}

ZTEST(meshtastic_core, test_settings_store_round_trip_owner)
{
	uint8_t original[TEST_OWNER_RECORD_LEN];
	uint8_t stored[TEST_OWNER_RECORD_LEN] = { TEST_STORE_RECORD_VERSION };
	int len;

	len = meshtastic_config_store_setting_get("owner", original, sizeof(original));
	zassert_equal(len, (int)sizeof(original));

	memcpy(&stored[TEST_OWNER_RECORD_LONG_NAME_OFF], "Stored Long", sizeof("Stored Long"));
	memcpy(&stored[TEST_OWNER_RECORD_SHORT_NAME_OFF], "SL", sizeof("SL"));

	zassert_ok(meshtastic_config_store_setting_set("owner", stored, sizeof(stored)));
	zassert_ok(meshtastic_config_store_apply_core());
	zassert_str_equal(meshtastic_long_name(), "Stored Long");
	zassert_str_equal(meshtastic_short_name(), "SL");

	zassert_ok(meshtastic_config_store_setting_set("owner", original, sizeof(original)));
	zassert_ok(meshtastic_config_store_apply_core());
}

ZTEST(meshtastic_core, test_settings_store_round_trip_core_config)
{
	meshtastic_Config config = meshtastic_Config_init_zero;
	meshtastic_Config original = meshtastic_Config_init_zero;
	uint8_t buf[MESHTASTIC_STORE_VALUE_MAX];
	int len;

	zassert_ok(meshtastic_config_store_get_config(meshtastic_Config_device_tag, &original));
	zassert_ok(meshtastic_config_store_get_config(meshtastic_Config_device_tag, &config));
	config.payload_variant.device.role = meshtastic_Config_DeviceConfig_Role_SENSOR;
	config.payload_variant.device.rebroadcast_mode =
		meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
	zassert_ok(meshtastic_config_store_set_config(&config));
	len = meshtastic_config_store_setting_get("config/device", buf, sizeof(buf));
	zassert_true(len > 0);

	config.payload_variant.device.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
	config.payload_variant.device.rebroadcast_mode =
		meshtastic_Config_DeviceConfig_RebroadcastMode_ALL;
	zassert_ok(meshtastic_config_store_set_config(&config));
	zassert_ok(meshtastic_config_store_setting_set("config/device", buf, (size_t)len));
	zassert_ok(meshtastic_config_store_apply_core());

	zassert_equal(meshtastic_device_role(), meshtastic_Config_DeviceConfig_Role_SENSOR);
	zassert_equal(meshtastic_rebroadcast_mode(),
		      meshtastic_Config_DeviceConfig_RebroadcastMode_NONE);

	zassert_ok(meshtastic_config_store_set_config(&original));
}

ZTEST(meshtastic_core, test_settings_store_round_trip_channel)
{
	meshtastic_Channel ch = meshtastic_Channel_init_zero;
	meshtastic_Channel original = meshtastic_Channel_init_zero;
	uint8_t buf[MESHTASTIC_STORE_VALUE_MAX];
	int len;

	zassert_ok(meshtastic_config_store_get_channel(2, &original));

	ch.index = 2;
	ch.role = meshtastic_Channel_Role_SECONDARY;
	ch.has_settings = true;
	strncpy(ch.settings.name, "Persist", sizeof(ch.settings.name) - 1U);
	ch.settings.psk.bytes[0] = 1U;
	ch.settings.psk.size = 1U;
	ch.settings.uplink_enabled = true;

	zassert_ok(meshtastic_config_store_set_channel(2, &ch));
	len = meshtastic_config_store_setting_get("channel/2", buf, sizeof(buf));
	zassert_true(len > 0);

	ch = (meshtastic_Channel)meshtastic_Channel_init_zero;
	ch.index = 2;
	ch.role = meshtastic_Channel_Role_DISABLED;
	ch.has_settings = true;
	zassert_ok(meshtastic_config_store_set_channel(2, &ch));
	zassert_ok(meshtastic_config_store_setting_set("channel/2", buf, (size_t)len));
	zassert_ok(meshtastic_config_store_apply_core());

	zassert_str_equal(meshtastic_channels_get_name(2), "Persist");
	zassert_equal(meshtastic_channels_get(2)->role, meshtastic_Channel_Role_SECONDARY);

	zassert_ok(meshtastic_config_store_set_channel(2, &original));
}

ZTEST(meshtastic_core, test_settings_store_round_trip_module_and_unsupported_module)
{
	meshtastic_ModuleConfig module = meshtastic_ModuleConfig_init_zero;
	meshtastic_ModuleConfig original_mqtt = meshtastic_ModuleConfig_init_zero;
	meshtastic_ModuleConfig original_status = meshtastic_ModuleConfig_init_zero;
	uint8_t buf[MESHTASTIC_STORE_VALUE_MAX];
	int len;

	zassert_ok(meshtastic_config_store_get_module(meshtastic_ModuleConfig_mqtt_tag,
						      &original_mqtt));
	zassert_ok(meshtastic_config_store_get_module(meshtastic_ModuleConfig_statusmessage_tag,
						      &original_status));

	module.which_payload_variant = meshtastic_ModuleConfig_mqtt_tag;
	strncpy(module.payload_variant.mqtt.address, "mqtt.example.test",
		sizeof(module.payload_variant.mqtt.address) - 1U);
	module.payload_variant.mqtt.enabled = true;
	zassert_ok(meshtastic_config_store_set_module(&module));
	len = meshtastic_config_store_setting_get("module/mqtt", buf, sizeof(buf));
	zassert_true(len > 0);

	module = (meshtastic_ModuleConfig)meshtastic_ModuleConfig_init_zero;
	module.which_payload_variant = meshtastic_ModuleConfig_mqtt_tag;
	zassert_ok(meshtastic_config_store_set_module(&module));
	zassert_ok(meshtastic_config_store_setting_set("module/mqtt", buf, (size_t)len));
	zassert_ok(meshtastic_config_store_get_module(meshtastic_ModuleConfig_mqtt_tag, &module));
	zassert_str_equal(module.payload_variant.mqtt.address, "mqtt.example.test");
	zassert_true(module.payload_variant.mqtt.enabled);

	module = (meshtastic_ModuleConfig)meshtastic_ModuleConfig_init_zero;
	module.which_payload_variant = meshtastic_ModuleConfig_statusmessage_tag;
	strncpy(module.payload_variant.statusmessage.node_status, "field retained",
		sizeof(module.payload_variant.statusmessage.node_status) - 1U);
	zassert_ok(meshtastic_config_store_set_module(&module));
	len = meshtastic_config_store_setting_get("module/statusmessage", buf, sizeof(buf));
	zassert_true(len > 0);
	module.payload_variant.statusmessage.node_status[0] = '\0';
	zassert_ok(meshtastic_config_store_set_module(&module));
	zassert_ok(meshtastic_config_store_setting_set("module/statusmessage", buf, (size_t)len));
	zassert_ok(meshtastic_config_store_get_module(meshtastic_ModuleConfig_statusmessage_tag,
						      &module));
	zassert_str_equal(module.payload_variant.statusmessage.node_status, "field retained");

	zassert_ok(meshtastic_config_store_set_module(&original_mqtt));
	zassert_ok(meshtastic_config_store_set_module(&original_status));
}

ZTEST(meshtastic_core, test_settings_store_rejects_invalid_record)
{
	meshtastic_Config config = meshtastic_Config_init_zero;
	meshtastic_Config after = meshtastic_Config_init_zero;
	uint8_t buf[MESHTASTIC_STORE_VALUE_MAX];
	int len;

	zassert_ok(meshtastic_config_store_get_config(meshtastic_Config_lora_tag, &config));
	len = meshtastic_config_store_setting_get("config/lora", buf, sizeof(buf));
	zassert_true(len > 0);

	buf[0] = 0xff;
	zassert_equal(meshtastic_config_store_setting_set("config/lora", buf, (size_t)len),
		      -EINVAL);

	zassert_ok(meshtastic_config_store_get_config(meshtastic_Config_lora_tag, &after));
	zassert_equal(after.payload_variant.lora.hop_limit, config.payload_variant.lora.hop_limit);

	len = meshtastic_config_store_setting_get("config/lora", buf, sizeof(buf));
	zassert_true(len > 0);
	sys_put_le16(0U, buf + 2U);
	zassert_equal(meshtastic_config_store_setting_set("config/lora", buf, (size_t)len),
		      -EINVAL);

	config.payload_variant.lora.hop_limit = 99U;
	len = (int)encode_config_store_record(&config, buf, sizeof(buf));
	zassert_equal(meshtastic_config_store_setting_set("config/lora", buf, (size_t)len),
		      -EINVAL);

	zassert_ok(meshtastic_config_store_get_config(meshtastic_Config_lora_tag, &after));
	zassert_not_equal(after.payload_variant.lora.hop_limit, 99U);
}

ZTEST(meshtastic_core, test_phoneapi_reports_settings_store_config)
{
	meshtastic_Config config = meshtastic_Config_init_zero;
	meshtastic_Config original = meshtastic_Config_init_zero;
	meshtastic_ToRadio to = meshtastic_ToRadio_init_zero;
	uint8_t buf[MESHTASTIC_API_FRAME_MAX];
	size_t len;
	bool saw_lora = false;
	bool saw_mqtt = false;

	test_phoneapi_ensure_initialized();

	zassert_ok(meshtastic_config_store_get_config(meshtastic_Config_lora_tag, &original));
	zassert_ok(meshtastic_config_store_get_config(meshtastic_Config_lora_tag, &config));
	config.payload_variant.lora.hop_limit = 6U;
	zassert_ok(meshtastic_config_store_set_config(&config));

	to.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
	to.want_config_id = 0x5678U;
	len = encode_toradio(&to, buf, sizeof(buf));
	meshtastic_phoneapi_handle_toradio(&test_phoneapi, buf, len);

	while (true) {
		meshtastic_FromRadio from = meshtastic_FromRadio_init_zero;

		if (!test_phoneapi_pop(&from)) {
			break;
		}

		if (from.which_payload_variant == meshtastic_FromRadio_config_tag &&
		    from.config.which_payload_variant == meshtastic_Config_lora_tag) {
			saw_lora = true;
			zassert_equal(from.config.payload_variant.lora.hop_limit, 6U);
		}

		if (from.which_payload_variant == meshtastic_FromRadio_moduleConfig_tag &&
		    from.moduleConfig.which_payload_variant == meshtastic_ModuleConfig_mqtt_tag) {
			saw_mqtt = true;
		}
	}

	zassert_true(saw_lora);
	zassert_true(saw_mqtt);

	zassert_ok(meshtastic_config_store_set_config(&original));
}
#endif /* CONFIG_MESHTASTIC_SETTINGS */

ZTEST(meshtastic_core, test_phoneapi_rebooted)
{
	meshtastic_FromRadio from = meshtastic_FromRadio_init_zero;

	test_phoneapi_ensure_initialized();

	meshtastic_phoneapi_enqueue_rebooted(&test_phoneapi);

	zassert_true(test_phoneapi_pop(&from));
	zassert_equal(from.which_payload_variant, meshtastic_FromRadio_rebooted_tag);
	zassert_true(from.rebooted);
	zassert_false(test_phoneapi_pop(&from));
}

ZTEST(meshtastic_core, test_phoneapi_packet_queue_status)
{
	const uint32_t dest = 0x0a0b0c0dU;
	meshtastic_ToRadio to = meshtastic_ToRadio_init_zero;
	meshtastic_FromRadio from = meshtastic_FromRadio_init_zero;
	uint8_t buf[MESHTASTIC_API_FRAME_MAX];
	uint32_t before = fake_data.send_count;
	size_t len;

	test_phoneapi_ensure_initialized();

	to.which_payload_variant = meshtastic_ToRadio_packet_tag;
	to.packet.to = dest;
	to.packet.id = 0x01020304U;
	to.packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
	to.packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
	to.packet.decoded.payload.size = 2U;
	memcpy(to.packet.decoded.payload.bytes, "hi", 2U);
	len = encode_toradio(&to, buf, sizeof(buf));

	meshtastic_phoneapi_handle_toradio(&test_phoneapi, buf, len);

	zassert_equal(fake_data.send_count, before + 1U);
	zassert_equal(last_tx_dest(), dest);
	zassert_true(test_phoneapi_pop(&from));
	zassert_equal(from.which_payload_variant, meshtastic_FromRadio_queueStatus_tag);
	zassert_equal(from.queueStatus.mesh_packet_id, 0x01020304U);
	zassert_equal(from.queueStatus.res, 0);
}

ZTEST(meshtastic_core, test_phoneapi_queue_overflow_drops_oldest)
{
	meshtastic_FromRadio from = meshtastic_FromRadio_init_zero;
	uint32_t first_remaining;

	test_phoneapi_ensure_initialized();

	for (uint32_t i = 0U; i < ARRAY_SIZE(test_phoneapi_queue) + 2U; i++) {
		meshtastic_phoneapi_enqueue_queue_status(&test_phoneapi, 0, i);
	}

	first_remaining = 2U;
	zassert_equal(meshtastic_phoneapi_pending_count(&test_phoneapi),
		      ARRAY_SIZE(test_phoneapi_queue));
	zassert_true(test_phoneapi_pop(&from));
	zassert_equal(from.which_payload_variant, meshtastic_FromRadio_queueStatus_tag);
	zassert_equal(from.queueStatus.mesh_packet_id, first_remaining);
}

ZTEST(meshtastic_core, test_phoneapi_current_frame_lifetime)
{
	struct meshtastic_phoneapi_frame frame;
	meshtastic_FromRadio from = meshtastic_FromRadio_init_zero;

	test_phoneapi_ensure_initialized();

	meshtastic_phoneapi_enqueue_queue_status(&test_phoneapi, 0, 1U);
	meshtastic_phoneapi_enqueue_queue_status(&test_phoneapi, 0, 2U);

	zassert_true(meshtastic_phoneapi_current_frame(&test_phoneapi, &frame));
	decode_fromradio_frame(&frame, &from);
	zassert_equal(from.queueStatus.mesh_packet_id, 1U);
	zassert_equal(meshtastic_phoneapi_pending_count(&test_phoneapi), 2U);

	from = (meshtastic_FromRadio)meshtastic_FromRadio_init_zero;
	zassert_true(meshtastic_phoneapi_current_frame(&test_phoneapi, &frame));
	decode_fromradio_frame(&frame, &from);
	zassert_equal(from.queueStatus.mesh_packet_id, 1U);

	meshtastic_phoneapi_current_frame_complete(&test_phoneapi);
	zassert_equal(meshtastic_phoneapi_pending_count(&test_phoneapi), 1U);

	from = (meshtastic_FromRadio)meshtastic_FromRadio_init_zero;
	zassert_true(meshtastic_phoneapi_current_frame(&test_phoneapi, &frame));
	decode_fromradio_frame(&frame, &from);
	zassert_equal(from.queueStatus.mesh_packet_id, 2U);
}
#endif

ZTEST_SUITE(meshtastic_core, NULL, meshtastic_setup, NULL, NULL, NULL);
