/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Meshtastic mesh radio stack implementation.
 *
 * Packet format (wire, up to 255 bytes total):
 *
 *   Offset  Size  Field
 *   ------  ----  ----------------------------------------------------
 *    0       4    Destination node ID (little-endian uint32)
 *    4       4    Source node ID      (little-endian uint32)
 *    8       4    Packet ID           (little-endian uint32)
 *   12       1    Flags: hop_limit[2:0] | want_ack[3] |
 *                        via_mqtt[4] | hop_start[7:5]
 *   13       1    Channel hash (DJB2-8 of channel name)
 *   14       1    Next-hop node (0 in this implementation)
 *   15       1    Relay node   (0 in this implementation)
 *   16      ≤239  AES-CTR encrypted nanopb-encoded Data protobuf
 *
 * Encryption (AES-CTR):
 *   Key  : channel PSK (16 or 32 bytes)
 *   Nonce: [packet_id LE32][zero32][src_node_id LE32][zero32]
 *
 * Routing:
 *   Simple flood relay: every received packet with hop_limit > 0
 *   that is not destined exclusively for this node is re-broadcast
 *   with hop_limit decremented by one.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/sys/byteorder.h>

#include <psa/crypto.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include <zephyr/meshtastic/meshtastic.h>

/* Generated nanopb headers (built from proto/mesh.proto). */
#include "mesh.pb.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(meshtastic, CONFIG_MESHTASTIC_LOG_LEVEL);

/* -------------------------------------------------------------------------
 * Wire-format constants
 */

/* Maximum raw LoRa packet size (bytes). */
#define PKT_MAX       255U
/* Wire-format header size (bytes). */
#define HDR_LEN       16U
/* Maximum encrypted payload size (bytes). */
#define PAYLOAD_MAX   (PKT_MAX - HDR_LEN)

/* Flags byte layout. */
#define FLAGS_HOP_LIMIT_MASK     0x07U
#define FLAGS_WANT_ACK           BIT(3)
#define FLAGS_VIA_MQTT           BIT(4)
#define FLAGS_HOP_START_SHIFT    5U
#define FLAGS_HOP_START_MASK     (0x07U << FLAGS_HOP_START_SHIFT)

/* -------------------------------------------------------------------------
 * Wire-format packet header (exactly 16 bytes, all fields little-endian).
 */
struct __packed mt_hdr {
	uint32_t dest;
	uint32_t src;
	uint32_t id;
	uint8_t  flags;
	uint8_t  channel;
	uint8_t  next_hop;
	uint8_t  relay_node;
};

BUILD_ASSERT(sizeof(struct mt_hdr) == HDR_LEN,
	     "mt_hdr size mismatch — must be exactly 16 bytes");

/* -------------------------------------------------------------------------
 * Default PSK for the LongFast channel.
 * Base64: "1PG7OiApB1nwvP+rz05pAQ=="
 */
const uint8_t meshtastic_default_psk[16] = {
	0xd4U, 0xf1U, 0xbbU, 0x3aU, 0x20U, 0x29U, 0x07U, 0x59U,
	0xf0U, 0xbcU, 0xffU, 0xabU, 0xcfU, 0x4eU, 0x69U, 0x01U,
};

/* -------------------------------------------------------------------------
 * Per-entry duplicate-detection cache record.
 */
struct dup_entry {
	uint32_t src;
	uint32_t id;
};

/* -------------------------------------------------------------------------
 * Stack-global state.
 */
static struct {
	const struct device  *lora_dev;
	uint32_t              node_id;
	uint8_t               psk[32];
	size_t                psk_len;
	uint8_t               ch_hash;
	uint8_t               hop_limit;
	int8_t                tx_power;
	uint32_t              frequency;
	meshtastic_recv_cb_t  recv_cb;
	uint32_t              next_pkt_id;
	struct dup_entry      dup_cache[CONFIG_MESHTASTIC_DUP_CACHE_SIZE];
	uint8_t               dup_head;
	struct k_mutex        lock; /* Serialises LoRa device access */
	bool                  initialized;
} mt;

/* Shared LoRa modem configuration; tx flag is updated before each call. */
static struct lora_modem_config mt_lora_cfg = {
	.bandwidth      = BW_250_KHZ,
	.datarate       = SF_11,
	.coding_rate    = CR_4_5,
	.preamble_len   = 16U,
	.iq_inverted    = false,
	.public_network = false,
	/* Meshtastic / RadioLib SX127-style byte; 0x2b -> 0x24b4 on SX126x */
	.sync_word      = 0x2b,
};

/* RX thread resources. */
static K_THREAD_STACK_DEFINE(mt_stack, CONFIG_MESHTASTIC_THREAD_STACK_SIZE);
static struct k_thread mt_thread;

/* -------------------------------------------------------------------------
 * Channel hash (Channels::generateHash in official Meshtastic firmware).
 *
 * XOR of the channel name and the channel PSK bytes.  Embedded in every
 * packet header so receivers can discard foreign channels before decrypting.
 */
static uint8_t xor_hash(const uint8_t *data, size_t len)
{
	uint8_t code = 0U;

	for (size_t i = 0; i < len; i++) {
		code ^= data[i];
	}

	return code;
}

static uint8_t channel_hash(const char *name, const uint8_t *psk, size_t psk_len)
{
	uint8_t h;

	h = xor_hash((const uint8_t *)name, strlen(name));
	h ^= xor_hash(psk, psk_len);

	return h;
}

/* -------------------------------------------------------------------------
 * Duplicate-packet detection.
 *
 * A small circular cache keyed on (src, packet_id) prevents a relayed
 * packet from being processed or re-relayed more than once per node.
 */
static bool dup_check(uint32_t src, uint32_t id)
{
	int i;

	for (i = 0; i < CONFIG_MESHTASTIC_DUP_CACHE_SIZE; i++) {
		if (mt.dup_cache[i].src == src &&
		    mt.dup_cache[i].id  == id) {
			return true;
		}
	}

	return false;
}

static void dup_add(uint32_t src, uint32_t id)
{
	mt.dup_cache[mt.dup_head].src = src;
	mt.dup_cache[mt.dup_head].id  = id;
	mt.dup_head = (uint8_t)((mt.dup_head + 1U) %
				CONFIG_MESHTASTIC_DUP_CACHE_SIZE);
}

/* -------------------------------------------------------------------------
 * AES-CTR via PSA Crypto.
 *
 * In CTR mode encryption and decryption are identical, so this single
 * function serves both directions.  @p out must be at least
 * (@p len + 16) bytes; the extra 16 bytes are a safety margin for the
 * PSA implementation and are never read by the caller.
 */
static int ctr_crypt(const uint8_t *key, size_t key_len,
		     const uint8_t nonce[16],
		     const uint8_t *in, uint8_t *out, size_t len)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_cipher_operation_t op  = PSA_CIPHER_OPERATION_INIT;
	psa_key_id_t kid           = PSA_KEY_ID_NULL;
	psa_status_t status;
	size_t out_len;
	size_t finish_len;
	int ret = 0;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
	psa_set_key_algorithm(&attr, PSA_ALG_CTR);
	psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attr, (uint32_t)(key_len * 8U));
	psa_set_key_lifetime(&attr, PSA_KEY_LIFETIME_VOLATILE);

	status = psa_import_key(&attr, key, key_len, &kid);
	if (status != PSA_SUCCESS) {
		LOG_ERR("psa_import_key failed (%d)", (int)status);
		return -EIO;
	}

	status = psa_cipher_encrypt_setup(&op, kid, PSA_ALG_CTR);
	if (status != PSA_SUCCESS) {
		LOG_ERR("psa_cipher_encrypt_setup failed (%d)", (int)status);
		ret = -EIO;
		goto destroy;
	}

	status = psa_cipher_set_iv(&op, nonce, 16U);
	if (status != PSA_SUCCESS) {
		LOG_ERR("psa_cipher_set_iv failed (%d)", (int)status);
		(void)psa_cipher_abort(&op);
		ret = -EIO;
		goto destroy;
	}

	status = psa_cipher_update(&op, in, len, out, len + 16U, &out_len);
	if (status != PSA_SUCCESS) {
		LOG_ERR("psa_cipher_update failed (%d)", (int)status);
		(void)psa_cipher_abort(&op);
		ret = -EIO;
		goto destroy;
	}

	status = psa_cipher_finish(&op, out + out_len, 16U, &finish_len);
	if (status != PSA_SUCCESS) {
		LOG_ERR("psa_cipher_finish failed (%d)", (int)status);
		ret = -EIO;
	}

destroy:
	(void)psa_destroy_key(kid);
	return ret;
}

/* -------------------------------------------------------------------------
 * Build a complete Meshtastic wire packet:
 *   16-byte header || AES-CTR-encrypted nanopb-encoded Data message.
 */
static int build_packet(uint32_t dest, uint8_t portnum,
			const uint8_t *payload, size_t payload_len,
			uint8_t *out, uint32_t *out_len)
{
	meshtastic_Data data;
	uint8_t pb_buf[PAYLOAD_MAX];
	/* Extra 16 bytes: PSA safety margin for ctr_crypt(). */
	uint8_t enc_buf[PAYLOAD_MAX + 16U];
	pb_ostream_t stream;
	uint32_t pkt_id;
	uint8_t nonce[16];
	struct mt_hdr *hdr;
	int ret;

	if (payload_len > 237U) {
		return -EINVAL;
	}

	/* Encode the Data protobuf. */
	data = (meshtastic_Data)meshtastic_Data_init_zero;
	data.portnum      = (meshtastic_PortNum)portnum;
	data.payload.size = (pb_size_t)payload_len;
	memcpy(data.payload.bytes, payload, payload_len);

	stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
	if (!pb_encode(&stream, meshtastic_Data_fields, &data)) {
		LOG_ERR("pb_encode failed");
		return -ENOMEM;
	}

	/* Allocate a monotonically increasing packet ID. */
	pkt_id = mt.next_pkt_id++;

	/*
	 * Construct the AES-CTR nonce (16 bytes):
	 *   bytes  0–3:  packet_id as LE uint32
	 *   bytes  4–7:  zero (upper 32 bits of 64-bit zero-extension)
	 *   bytes  8–11: source node ID as LE uint32
	 *   bytes 12–15: zero
	 */
	memset(nonce, 0, sizeof(nonce));
	sys_put_le32(pkt_id,    nonce);
	sys_put_le32(mt.node_id, nonce + 8U);

	ret = ctr_crypt(mt.psk, mt.psk_len, nonce,
			pb_buf, enc_buf, stream.bytes_written);
	if (ret < 0) {
		return ret;
	}

	/* Write the 16-byte wire header. */
	hdr             = (struct mt_hdr *)out;
	hdr->dest       = sys_cpu_to_le32(dest);
	hdr->src        = sys_cpu_to_le32(mt.node_id);
	hdr->id         = sys_cpu_to_le32(pkt_id);
	hdr->flags      = (mt.hop_limit & FLAGS_HOP_LIMIT_MASK) |
			  ((mt.hop_limit & 0x07U) << FLAGS_HOP_START_SHIFT);
	hdr->channel    = mt.ch_hash;
	hdr->next_hop   = 0U;
	hdr->relay_node = 0U;

	memcpy(out + HDR_LEN, enc_buf, stream.bytes_written);
	*out_len = (uint32_t)(HDR_LEN + stream.bytes_written);

	return 0;
}

/* -------------------------------------------------------------------------
 * Forward declaration (process_rx and the relay path are mutually aware).
 */
static void process_rx(const uint8_t *buf, int len, int16_t rssi, int8_t snr);

/* -------------------------------------------------------------------------
 * Flood relay: re-broadcast a received packet with hop_limit decremented.
 *
 * Called from process_rx() which holds no locks, so this function may
 * safely acquire mt.lock.
 */
static void relay_packet(const uint8_t *buf, int len,
			 const struct mt_hdr *hdr, uint8_t hop_limit)
{
	uint8_t relay_buf[PKT_MAX];
	struct mt_hdr *relay_hdr;
	int ret;

	if (len > (int)PKT_MAX) {
		return;
	}

	memcpy(relay_buf, buf, (size_t)len);
	relay_hdr        = (struct mt_hdr *)relay_buf;
	relay_hdr->flags = (hdr->flags & ~FLAGS_HOP_LIMIT_MASK) |
			   ((hop_limit - 1U) & FLAGS_HOP_LIMIT_MASK);

	k_mutex_lock(&mt.lock, K_FOREVER);

	mt_lora_cfg.frequency = mt.frequency;
	mt_lora_cfg.tx_power  = mt.tx_power;
	mt_lora_cfg.tx        = true;

	ret = lora_config(mt.lora_dev, &mt_lora_cfg);
	if (ret == 0) {
		(void)lora_send(mt.lora_dev, relay_buf, (uint32_t)len);
	} else {
		LOG_ERR("lora_config for relay failed (%d)", ret);
	}

	k_mutex_unlock(&mt.lock);
}

/* -------------------------------------------------------------------------
 * Receive processing: authenticate, decrypt, decode, deliver, relay.
 */
static void process_rx(const uint8_t *buf, int len, int16_t rssi, int8_t snr)
{
	const struct mt_hdr *hdr;
	uint32_t dest;
	uint32_t src;
	uint32_t pkt_id;
	uint8_t hop_limit;
	const uint8_t *enc_payload;
	size_t enc_len;
	uint8_t nonce[16];
	/* Extra 16 bytes: PSA safety margin for ctr_crypt(). */
	uint8_t dec_buf[PAYLOAD_MAX + 16U];
	meshtastic_Data data;
	pb_istream_t stream;
	int ret;

	if (len < (int)HDR_LEN) {
		LOG_DBG("Packet too short (%d bytes)", len);
		return;
	}

	hdr       = (const struct mt_hdr *)buf;
	dest      = sys_le32_to_cpu(hdr->dest);
	src       = sys_le32_to_cpu(hdr->src);
	pkt_id    = sys_le32_to_cpu(hdr->id);
	hop_limit = hdr->flags & FLAGS_HOP_LIMIT_MASK;

	/* Filter by channel hash to avoid unnecessary decryption. */
	if (hdr->channel != mt.ch_hash) {
		LOG_DBG("Channel hash mismatch (0x%02x vs 0x%02x)",
			hdr->channel, mt.ch_hash);
		return;
	}

	/* Deduplicate to break relay loops. */
	if (dup_check(src, pkt_id)) {
		LOG_DBG("Duplicate (src=0x%08x id=0x%08x)", src, pkt_id);
		return;
	}
	dup_add(src, pkt_id);

	enc_payload = buf + HDR_LEN;
	enc_len     = (size_t)(len - (int)HDR_LEN);

	/* Decrypt using the per-packet nonce. */
	memset(nonce, 0, sizeof(nonce));
	sys_put_le32(pkt_id, nonce);
	sys_put_le32(src,    nonce + 8U);

	ret = ctr_crypt(mt.psk, mt.psk_len, nonce,
			enc_payload, dec_buf, enc_len);
	if (ret < 0) {
		LOG_ERR("Decryption error (%d)", ret);
		return;
	}

	/* Decode the Data protobuf. */
	data   = (meshtastic_Data)meshtastic_Data_init_zero;
	stream = pb_istream_from_buffer(dec_buf, enc_len);
	if (!pb_decode(&stream, meshtastic_Data_fields, &data)) {
		/*
		 * A failed decode usually means the packet is encrypted
		 * with a different key — not an error worth logging at ERR.
		 */
		LOG_DBG("pb_decode failed (wrong channel key?)");
		return;
	}

	LOG_INF("RX from 0x%08x to 0x%08x port=%u len=%u rssi=%d snr=%d",
		src, dest, (unsigned int)data.portnum,
		(unsigned int)data.payload.size, (int)rssi, (int)snr);

	/* Deliver packets addressed to this node or broadcast. */
	if (dest == mt.node_id || dest == MESHTASTIC_NODE_BROADCAST) {
		if (mt.recv_cb != NULL) {
			mt.recv_cb(src, dest,
				   (uint8_t)data.portnum,
				   data.payload.bytes,
				   (size_t)data.payload.size,
				   rssi, snr);
		}
	}

	/* Flood-relay packets that still have remaining hops. */
	if (hop_limit > 0U && dest != mt.node_id) {
		relay_packet(buf, len, hdr, hop_limit);
	}
}

/* -------------------------------------------------------------------------
 * Background receive thread.
 *
 * Holds mt.lock only during lora_config() so meshtastic_send_data() is not
 * blocked for the full lora_recv() timeout.  The modem is still serialised:
 * send holds the lock for config + lora_send.
 */
static void mt_thread_fn(void *p1, void *p2, void *p3)
{
	uint8_t buf[PKT_MAX];
	int16_t rssi;
	int8_t  snr;
	int     ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_mutex_lock(&mt.lock, K_FOREVER);

		mt_lora_cfg.frequency = mt.frequency;
		mt_lora_cfg.tx_power  = mt.tx_power;
		mt_lora_cfg.tx        = false;

		ret = lora_config(mt.lora_dev, &mt_lora_cfg);

		k_mutex_unlock(&mt.lock);

		if (ret < 0) {
			LOG_ERR("lora_config (RX) failed (%d)", ret);
			k_sleep(K_MSEC(100));
			continue;
		}

		ret = lora_recv(mt.lora_dev, buf, (uint8_t)sizeof(buf),
				K_MSEC(CONFIG_MESHTASTIC_RX_TIMEOUT_MS),
				&rssi, &snr);

		if (ret > 0) {
			process_rx(buf, ret, rssi, snr);
		}
	}
}

/* -------------------------------------------------------------------------
 * Public API
 */

int meshtastic_init(const struct meshtastic_config *cfg)
{
	psa_status_t psa_st;
	int ret;

	if (cfg == NULL) {
		return -EINVAL;
	}

	if (cfg->lora_dev == NULL || !device_is_ready(cfg->lora_dev)) {
		LOG_ERR("LoRa device not ready");
		return -ENODEV;
	}

	if (cfg->psk == NULL ||
	    (cfg->psk_len != 16U && cfg->psk_len != 32U)) {
		LOG_ERR("Invalid PSK (len=%zu)", cfg->psk_len);
		return -EINVAL;
	}

	if (cfg->channel_name == NULL || cfg->frequency == 0U) {
		return -EINVAL;
	}

	mt.lora_dev  = cfg->lora_dev;
	mt.node_id   = cfg->node_id;
	mt.psk_len   = cfg->psk_len;
	memcpy(mt.psk, cfg->psk, cfg->psk_len);

	mt.ch_hash   = channel_hash(cfg->channel_name, mt.psk, mt.psk_len);
	mt.frequency = cfg->frequency;

	mt.hop_limit = (cfg->hop_limit == 0U)
		? (uint8_t)CONFIG_MESHTASTIC_DEFAULT_HOP_LIMIT
		: cfg->hop_limit;

	mt.tx_power  = (cfg->tx_power == 0)
		? (int8_t)CONFIG_MESHTASTIC_TX_POWER
		: cfg->tx_power;

	mt.next_pkt_id = 1U;
	mt.dup_head    = 0U;
	memset(mt.dup_cache, 0, sizeof(mt.dup_cache));

	/* Initialise PSA crypto subsystem. */
	psa_st = psa_crypto_init();
	if (psa_st != PSA_SUCCESS) {
		LOG_ERR("psa_crypto_init failed (%d)", (int)psa_st);
		return -EIO;
	}

	/* Perform an initial RX modem configuration to validate settings. */
	mt_lora_cfg.frequency = mt.frequency;
	mt_lora_cfg.tx_power  = mt.tx_power;
	mt_lora_cfg.tx        = false;

	ret = lora_config(mt.lora_dev, &mt_lora_cfg);
	if (ret < 0) {
		LOG_ERR("Initial lora_config failed (%d)", ret);
		return -EIO;
	}

	k_mutex_init(&mt.lock);

	k_thread_create(&mt_thread, mt_stack,
			K_THREAD_STACK_SIZEOF(mt_stack),
			mt_thread_fn, NULL, NULL, NULL,
			CONFIG_MESHTASTIC_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&mt_thread, "meshtastic");

	mt.initialized = true;

	LOG_INF("Meshtastic init: node=0x%08x ch_hash=0x%02x freq=%uHz",
		mt.node_id, mt.ch_hash, mt.frequency);

	return 0;
}

int meshtastic_send_data(uint32_t dest, uint8_t portnum,
			 const uint8_t *payload, size_t payload_len)
{
	uint8_t pkt[PKT_MAX];
	uint32_t pkt_len;
	int ret;

	if (!mt.initialized) {
		return -EINVAL;
	}

	if (payload == NULL || payload_len == 0U || payload_len > 237U) {
		return -EINVAL;
	}

	ret = build_packet(dest, portnum, payload, payload_len,
			   pkt, &pkt_len);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&mt.lock, K_FOREVER);

	mt_lora_cfg.frequency = mt.frequency;
	mt_lora_cfg.tx_power  = mt.tx_power;
	mt_lora_cfg.tx        = true;

	ret = lora_config(mt.lora_dev, &mt_lora_cfg);
	if (ret == 0) {
		ret = lora_send(mt.lora_dev, pkt, pkt_len);
		if (ret < 0) {
			LOG_ERR("lora_send failed (%d)", ret);
		} else {
			LOG_INF("TX to 0x%08x port=%u len=%u",
				dest, (unsigned int)portnum,
				(unsigned int)pkt_len);
		}
	} else {
		LOG_ERR("lora_config (TX) failed (%d)", ret);
	}

	k_mutex_unlock(&mt.lock);

	return ret;
}

int meshtastic_send_text(uint32_t dest, const char *text)
{
	size_t len;

	if (text == NULL) {
		return -EINVAL;
	}

	len = strlen(text);
	if (len == 0U || len > MESHTASTIC_MAX_TEXT_LEN) {
		return -EINVAL;
	}

	return meshtastic_send_data(dest, MESHTASTIC_PORT_TEXT_MESSAGE,
				    (const uint8_t *)text, len);
}

void meshtastic_set_recv_cb(meshtastic_recv_cb_t cb)
{
	mt.recv_cb = cb;
}

uint32_t meshtastic_get_node_id(void)
{
	return mt.node_id;
}
