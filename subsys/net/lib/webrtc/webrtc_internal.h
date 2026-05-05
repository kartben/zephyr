/** @file
 * @brief WebRTC private internal header.
 *
 * Not to be included by applications.
 */

/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SUBSYS_NET_LIB_WEBRTC_WEBRTC_INTERNAL_H_
#define SUBSYS_NET_LIB_WEBRTC_WEBRTC_INTERNAL_H_

#include <zephyr/kernel.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/webrtc.h>

/* -------------------------------------------------------------------------
 * Compile-time limits
 * ---------------------------------------------------------------------- */

/** Maximum number of ICE candidates stored per side (local or remote). */
#define WEBRTC_MAX_ICE_CANDIDATES CONFIG_WEBRTC_MAX_ICE_CANDIDATES

/** Length of ICE ufrag (excluding NUL). */
#define WEBRTC_ICE_UFRAG_LEN  8U

/** Length of ICE password (excluding NUL). */
#define WEBRTC_ICE_PWD_LEN   24U

/** SHA-256 digest length in bytes. */
#define WEBRTC_FINGERPRINT_LEN 32U

/**
 * Formatted fingerprint length:
 *   32 hex pairs separated by ':' plus NUL  →  32*2 + 31 + 1 = 96 bytes
 */
#define WEBRTC_FINGERPRINT_STR_LEN 96U

/** Maximum size of a candidate attribute value string (NUL excluded). */
#define WEBRTC_CANDIDATE_STR_LEN 128U

/* -------------------------------------------------------------------------
 * Data-channel wire protocol (Zephyr-specific framing over DTLS)
 *
 * Message header layout (4 bytes):
 *   byte 0-1: channel ID (big-endian uint16_t)
 *   byte 2:   message type
 *   byte 3:   reserved, must be 0
 *
 * Message types:
 * ---------------------------------------------------------------------- */

#define WEBRTC_DC_MSG_OPEN  0x01U /**< Channel-open request  */
#define WEBRTC_DC_MSG_ACK   0x02U /**< Channel-open acknowledgement */
#define WEBRTC_DC_MSG_DATA  0x03U /**< User data */
#define WEBRTC_DC_MSG_CLOSE 0x04U /**< Channel-close notification */

#define WEBRTC_DC_HEADER_LEN 4U

/* -------------------------------------------------------------------------
 * STUN constants (RFC 5389 / RFC 8489)
 * ---------------------------------------------------------------------- */

#define STUN_MAGIC_COOKIE        0x2112A442U
#define STUN_HEADER_LEN          20U
#define STUN_TRANSACTION_ID_LEN  12U

/* Message types */
#define STUN_MSG_BINDING_REQUEST  0x0001U
#define STUN_MSG_BINDING_RESPONSE 0x0101U
#define STUN_MSG_BINDING_ERROR    0x0111U

/* Attribute types */
#define STUN_ATTR_USERNAME         0x0006U
#define STUN_ATTR_MESSAGE_INTEGRITY 0x0008U
#define STUN_ATTR_ERROR_CODE        0x0009U
#define STUN_ATTR_XOR_MAPPED_ADDR  0x0020U
#define STUN_ATTR_PRIORITY         0x0024U
#define STUN_ATTR_USE_CANDIDATE    0x0025U
#define STUN_ATTR_FINGERPRINT      0x8028U
#define STUN_ATTR_ICE_CONTROLLED   0x8029U
#define STUN_ATTR_ICE_CONTROLLING  0x802AU

/* Address family in STUN mapped-address */
#define STUN_ADDR_FAMILY_IPV4 0x01U
#define STUN_ADDR_FAMILY_IPV6 0x02U

/* -------------------------------------------------------------------------
 * ICE candidate
 * ---------------------------------------------------------------------- */

/** Parsed ICE host candidate. */
struct webrtc_ice_candidate {
	/** Human-readable address string (IPv4 dotted-decimal or IPv6). */
	char addr[INET6_ADDRSTRLEN];
	/** Port number in host byte order. */
	uint16_t port;
	/** Component ID (always 1 — RTP component — for data channels). */
	uint8_t component;
	/** ICE priority value computed per RFC 8445 §5.1.2. */
	uint32_t priority;
	/** True when this entry holds a valid candidate. */
	bool valid;
};

/* -------------------------------------------------------------------------
 * Parsed SDP information
 * ---------------------------------------------------------------------- */

/** Information extracted from or written into one SDP m= section. */
struct webrtc_sdp_info {
	/** ICE username fragment (NUL-terminated). */
	char ice_ufrag[WEBRTC_ICE_UFRAG_LEN + 1U];

	/** ICE password (NUL-terminated). */
	char ice_pwd[WEBRTC_ICE_PWD_LEN + 1U];

	/** SHA-256 fingerprint of the DTLS certificate (raw bytes). */
	uint8_t fingerprint[WEBRTC_FINGERPRINT_LEN];

	/** True when @p fingerprint has been populated. */
	bool fingerprint_valid;

	/**
	 * DTLS setup role: true → active (client), false → passive (server).
	 * Derived from the SDP @c a=setup: attribute.
	 */
	bool dtls_active;

	/** ICE candidates included in the SDP. */
	struct webrtc_ice_candidate candidates[WEBRTC_MAX_ICE_CANDIDATES];

	/** Number of valid entries in @p candidates. */
	uint8_t candidate_count;
};

/* -------------------------------------------------------------------------
 * Internal peer connection state
 * ---------------------------------------------------------------------- */

/** Internal state values for the peer connection state machine. */
enum webrtc_pc_internal_state {
	WEBRTC_PC_INT_NEW,
	WEBRTC_PC_INT_HAVE_LOCAL_OFFER,
	WEBRTC_PC_INT_HAVE_REMOTE_OFFER,
	WEBRTC_PC_INT_GATHERING,
	WEBRTC_PC_INT_CHECKING,
	WEBRTC_PC_INT_CONNECTED,
	WEBRTC_PC_INT_FAILED,
	WEBRTC_PC_INT_CLOSED,
};

/** Internal data channel state. */
struct rtc_data_channel {
	/** Back-pointer to owning peer connection. */
	struct rtc_peer_connection *pc;

	/** Channel label (NUL-terminated). */
	char label[CONFIG_WEBRTC_DATA_CHANNEL_LABEL_LEN + 1U];

	/** Channel ID negotiated during open handshake. */
	uint16_t id;

	/** Ready-state visible to the application. */
	enum rtc_data_channel_state state;

	/** Application-registered callbacks. */
	struct rtc_data_channel_callbacks cbs;

	/** User data forwarded to callbacks. */
	void *user_data;

	/** True if messages must be delivered in order. */
	bool ordered;
};

/** Internal peer connection context.  One per active peer connection. */
struct rtc_peer_connection {
	/* ---- Configuration and callbacks --------------------------------- */

	/** Configuration supplied at creation time. */
	struct rtc_configuration config;

	/** Application-registered callbacks. */
	struct rtc_peer_connection_callbacks cbs;

	/** User data forwarded to callbacks. */
	void *user_data;

	/* ---- State -------------------------------------------------------- */

	/** Internal (fine-grained) state. */
	enum webrtc_pc_internal_state int_state;

	/** Mutex protecting this context. */
	struct k_mutex lock;

	/** Reference count.  Freed when it reaches zero. */
	atomic_t refcount;

	/* ---- Session description ----------------------------------------- */

	/** Information derived from the local SDP. */
	struct webrtc_sdp_info local_sdp;

	/** Information derived from the remote SDP. */
	struct webrtc_sdp_info remote_sdp;

	/**
	 * True once the remote description has been applied (so we know
	 * whether we are the offerer or the answerer).
	 */
	bool have_remote_desc;

	/**
	 * True if we generated the offer (we are the controlling agent in
	 * ICE).
	 */
	bool is_offerer;

	/* ---- ICE ---------------------------------------------------------- */

	/** UDP socket used for ICE connectivity checks and data I/O. */
	int ice_sock;

	/** Local ICE candidates gathered from local interfaces. */
	struct webrtc_ice_candidate
		local_candidates[WEBRTC_MAX_ICE_CANDIDATES];

	/** Number of valid local candidates. */
	uint8_t local_candidate_count;

	/** Index into local_candidates for the selected (nominated) pair. */
	int8_t selected_local_idx;

	/** Index into remote_sdp.candidates for the selected pair. */
	int8_t selected_remote_idx;

	/** ICE tiebreaker value (used to resolve role conflicts). */
	uint64_t ice_tiebreaker;

	/* ---- DTLS --------------------------------------------------------- */

	/** DTLS socket layered on top of the selected ICE socket. */
	int dtls_sock;

	/** True once the DTLS handshake has completed. */
	bool dtls_connected;

	/* ---- Data channels ----------------------------------------------- */

	/** Data channel table.  Slots are NULL when unused. */
	struct rtc_data_channel
		*channels[CONFIG_WEBRTC_MAX_DATA_CHANNELS];

	/** Next channel ID to assign when creating a new channel. */
	uint16_t next_channel_id;

	/* ---- Background I/O work ----------------------------------------- */

	/** Delayed work item that drives ICE checks and data reception. */
	struct k_work_delayable io_work;
};

/* -------------------------------------------------------------------------
 * Internal function declarations (cross-file)
 * ---------------------------------------------------------------------- */

/* webrtc_sdp.c */
int webrtc_sdp_generate_offer(struct rtc_peer_connection *pc,
			      char *buf, size_t buf_len);
int webrtc_sdp_generate_answer(struct rtc_peer_connection *pc,
			       char *buf, size_t buf_len);
int webrtc_sdp_parse(const char *sdp, struct webrtc_sdp_info *out);

/* webrtc_ice.c */
int webrtc_ice_gather_candidates(struct rtc_peer_connection *pc);
int webrtc_ice_start_checks(struct rtc_peer_connection *pc);
void webrtc_ice_process_packet(struct rtc_peer_connection *pc,
			       const uint8_t *buf, size_t len,
			       const struct sockaddr *from);
bool webrtc_is_stun_packet(const uint8_t *buf, size_t len);
int webrtc_stun_send_binding_request(struct rtc_peer_connection *pc,
				     int sock,
				     const struct sockaddr *dest,
				     const char *remote_ufrag,
				     const char *remote_pwd,
				     const char *local_ufrag,
				     bool use_candidate);

/* webrtc_dtls.c */
int webrtc_dtls_connect(struct rtc_peer_connection *pc);
void webrtc_dtls_close(struct rtc_peer_connection *pc);

/* webrtc_datachannel.c */
int webrtc_dc_send_open(struct rtc_peer_connection *pc,
			struct rtc_data_channel *dc);
int webrtc_dc_process_packet(struct rtc_peer_connection *pc,
			     const uint8_t *buf, size_t len);
struct rtc_data_channel *webrtc_dc_find_by_id(
	struct rtc_peer_connection *pc, uint16_t id);
void webrtc_dc_close_all(struct rtc_peer_connection *pc);

/* webrtc.c */
void webrtc_pc_set_connection_state(struct rtc_peer_connection *pc,
				    enum rtc_peer_connection_state state);
void webrtc_pc_set_gathering_state(struct rtc_peer_connection *pc,
				   enum rtc_ice_gathering_state state);
void webrtc_pc_io_work_handler(struct k_work *work);

#endif /* SUBSYS_NET_LIB_WEBRTC_WEBRTC_INTERNAL_H_ */
