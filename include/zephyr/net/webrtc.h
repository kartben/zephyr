/** @file
 * @brief WebRTC Data Channel API
 *
 * An API for applications to set up WebRTC peer connections and
 * exchange data via RTCDataChannels.
 */

/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_NET_WEBRTC_H_
#define ZEPHYR_INCLUDE_NET_WEBRTC_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/tls_credentials.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WebRTC Data Channel API
 * @defgroup webrtc WebRTC Data Channel API
 * @since 4.2
 * @version 0.1.0
 * @ingroup networking
 * @{
 */

/**
 * @note This implementation is **EXPERIMENTAL**.
 *
 * The data channel transport uses a Zephyr-specific lightweight framing
 * over DTLS rather than the standard SCTP-over-DTLS specified by
 * :rfc:`8832`.  Interoperability with browsers or standard WebRTC endpoints
 * therefore requires a future SCTP upgrade.  The API surface, Kconfig
 * symbols, and SDP dialect may change between releases.
 *
 * ICE candidate gathering is limited to host candidates (local interface
 * addresses).  Server-reflexive and relay candidates (TURN) are not
 * gathered in this version.
 */

/** Maximum length of a data channel label string (not including NUL). */
#define RTC_DATA_CHANNEL_LABEL_MAX_LEN CONFIG_WEBRTC_DATA_CHANNEL_LABEL_LEN

/**
 * @brief RTCPeerConnection state.
 *
 * Models the W3C RTCPeerConnectionState enum.
 */
enum rtc_peer_connection_state {
	/** Connection object created, no ICE activity yet. */
	RTC_PEER_CONNECTION_NEW,
	/** ICE/DTLS negotiation in progress. */
	RTC_PEER_CONNECTION_CONNECTING,
	/** ICE and DTLS handshake completed; data channels may be open. */
	RTC_PEER_CONNECTION_CONNECTED,
	/** Connection temporarily lost; attempting recovery. */
	RTC_PEER_CONNECTION_DISCONNECTED,
	/** Connection failed and cannot be recovered. */
	RTC_PEER_CONNECTION_FAILED,
	/** Connection has been closed. */
	RTC_PEER_CONNECTION_CLOSED,
};

/**
 * @brief ICE gathering state.
 *
 * Models the W3C RTCIceGatheringState enum.
 */
enum rtc_ice_gathering_state {
	/** No gathering has started. */
	RTC_ICE_GATHERING_NEW,
	/** Gathering ICE candidates. */
	RTC_ICE_GATHERING_GATHERING,
	/** Gathering complete. */
	RTC_ICE_GATHERING_COMPLETE,
};

/**
 * @brief ICE connection state.
 *
 * Models the W3C RTCIceConnectionState enum.
 */
enum rtc_ice_connection_state {
	/** ICE agent is idle. */
	RTC_ICE_CONNECTION_NEW,
	/** Connectivity checks in progress. */
	RTC_ICE_CONNECTION_CHECKING,
	/** At least one candidate pair is usable. */
	RTC_ICE_CONNECTION_CONNECTED,
	/** ICE failed; no usable candidate pair found. */
	RTC_ICE_CONNECTION_FAILED,
	/** ICE connection closed. */
	RTC_ICE_CONNECTION_CLOSED,
};

/**
 * @brief RTCDataChannel ready-state.
 *
 * Models the W3C RTCDataChannelState enum.
 */
enum rtc_data_channel_state {
	/** Waiting for DTLS handshake to complete. */
	RTC_DATA_CHANNEL_CONNECTING,
	/** Channel is open and data may be sent/received. */
	RTC_DATA_CHANNEL_OPEN,
	/** Channel close in progress. */
	RTC_DATA_CHANNEL_CLOSING,
	/** Channel is closed. */
	RTC_DATA_CHANNEL_CLOSED,
};

/** Forward declarations. */
struct rtc_peer_connection;
struct rtc_data_channel;

/**
 * @brief ICE server configuration.
 *
 * Only STUN URLs are supported in this version; TURN is not.
 * Example: @c "stun:stun.example.com:3478"
 */
struct rtc_ice_server {
	/** NUL-terminated URL string for the ICE server. */
	const char *urls;
};

/**
 * @brief RTCConfiguration passed to rtc_peer_connection_create().
 */
struct rtc_configuration {
	/**
	 * Array of ICE servers.  May be NULL if no STUN server is needed
	 * (host-only candidates will still be gathered).
	 */
	const struct rtc_ice_server *ice_servers;

	/** Number of entries in @p ice_servers. */
	size_t ice_servers_count;

	/**
	 * TLS credential tag that carries the DTLS certificate and
	 * private key (registered via tls_credential_add()).  The tag
	 * must provide a TLS_CREDENTIAL_PUBLIC_CERTIFICATE and a
	 * TLS_CREDENTIAL_PRIVATE_KEY with the same tag value.
	 *
	 * Set to a negative value to use a self-signed certificate
	 * generated at runtime (requires CONFIG_WEBRTC_DTLS_SELFSIGNED).
	 */
	sec_tag_t dtls_tag;
};

/**
 * @brief Initialiser options for an RTCDataChannel.
 */
struct rtc_data_channel_init {
	/**
	 * When true the transport layer preserves message order.
	 * When false messages may be delivered out of order for lower
	 * latency.  Defaults to true.
	 */
	bool ordered;

	/**
	 * Maximum number of times a message is retransmitted before the
	 * channel is closed.  Use 0 for unlimited retransmits (reliable).
	 * Only meaningful when @p ordered is true.
	 */
	uint16_t max_retransmits;

	/**
	 * Optional sub-protocol string (NUL-terminated).  May be NULL.
	 */
	const char *protocol;
};

/**
 * @brief Callbacks dispatched for RTCPeerConnection events.
 *
 * All callbacks are invoked from a system work-queue thread.  The
 * application must not call blocking functions from within a callback.
 * All pointers may be NULL to disable individual notifications.
 */
struct rtc_peer_connection_callbacks {
	/**
	 * @brief An ICE candidate has been gathered and should be sent to
	 *        the remote peer via out-of-band signalling.
	 *
	 * @p candidate is a NUL-terminated ICE candidate attribute string in
	 * the SDP @c a=candidate: format (the @c a= prefix and line ending
	 * are omitted).  The string is only valid for the duration of the
	 * callback; copy it if needed.
	 *
	 * When @p candidate is NULL, gathering is complete (equivalent to
	 * the W3C @c icecandidate event with a null candidate).
	 */
	void (*on_ice_candidate)(struct rtc_peer_connection *pc,
				 const char *candidate,
				 void *user_data);

	/**
	 * @brief Peer connection state changed.
	 *
	 * @p state reflects the new value of
	 * @ref rtc_peer_connection_state.
	 */
	void (*on_connection_state_change)(struct rtc_peer_connection *pc,
					   enum rtc_peer_connection_state state,
					   void *user_data);

	/**
	 * @brief ICE gathering state changed.
	 */
	void (*on_gathering_state_change)(struct rtc_peer_connection *pc,
					  enum rtc_ice_gathering_state state,
					  void *user_data);

	/**
	 * @brief A new data channel was opened by the remote peer.
	 *
	 * The application should register callbacks on @p dc using
	 * rtc_data_channel_set_callbacks() and may send data as soon as
	 * this callback returns.
	 */
	void (*on_data_channel)(struct rtc_peer_connection *pc,
				struct rtc_data_channel *dc,
				void *user_data);
};

/**
 * @brief Callbacks dispatched for RTCDataChannel events.
 *
 * All callbacks are invoked from a system work-queue thread.  All
 * pointers may be NULL to disable individual notifications.
 */
struct rtc_data_channel_callbacks {
	/**
	 * @brief Data channel is open and ready for use.
	 */
	void (*on_open)(struct rtc_data_channel *dc, void *user_data);

	/**
	 * @brief Data channel has been closed.
	 *
	 * After this callback the channel handle is no longer valid.
	 */
	void (*on_close)(struct rtc_data_channel *dc, void *user_data);

	/**
	 * @brief A message was received on the data channel.
	 *
	 * @p data points to the payload; it is valid only for the duration
	 * of this callback.  Copy it if the data must outlive the callback.
	 */
	void (*on_message)(struct rtc_data_channel *dc,
			   const uint8_t *data, size_t len,
			   void *user_data);

	/**
	 * @brief An error occurred on the data channel.
	 *
	 * @p err is a negative errno value describing the error.
	 */
	void (*on_error)(struct rtc_data_channel *dc, int err,
			 void *user_data);
};

/* -------------------------------------------------------------------------
 * Peer connection API
 * ---------------------------------------------------------------------- */

/**
 * @brief Create a new RTCPeerConnection.
 *
 * The peer connection begins in the @ref RTC_PEER_CONNECTION_NEW state.
 * No network activity occurs until rtc_peer_connection_set_local_description()
 * or rtc_peer_connection_set_remote_description() is called.
 *
 * @param config    Configuration for the connection.  The caller retains
 *                  ownership; the structure is copied internally.
 * @param cbs       Event callbacks.  May be NULL if no events are needed.
 *                  The structure is copied internally.
 * @param user_data Opaque value forwarded to every callback.
 * @param[out] pc   Set to the new peer connection handle on success.
 *
 * @retval 0       Success.
 * @retval -ENOMEM No free peer connection context available.
 * @retval -EINVAL Invalid argument.
 */
int rtc_peer_connection_create(const struct rtc_configuration *config,
			       const struct rtc_peer_connection_callbacks *cbs,
			       void *user_data,
			       struct rtc_peer_connection **pc);

/**
 * @brief Close and release a peer connection.
 *
 * Any open data channels are closed.  ICE and DTLS resources are freed.
 * The handle must not be used after this call.
 *
 * @param pc Peer connection handle returned by rtc_peer_connection_create().
 */
void rtc_peer_connection_close(struct rtc_peer_connection *pc);

/**
 * @brief Generate an SDP offer.
 *
 * Prepares the local session description and populates @p sdp_buf with the
 * resulting SDP string (NUL-terminated).  ICE gathering starts when
 * rtc_peer_connection_set_local_description() is subsequently called with
 * this SDP.
 *
 * @param pc      Peer connection handle.
 * @param sdp_buf Buffer that receives the SDP offer string.
 * @param sdp_len Size of @p sdp_buf in bytes.
 *
 * @retval 0       Success.
 * @retval -ENOSPC Buffer too small.
 * @retval -EINVAL Invalid state (remote description already set without
 *                 answering first).
 */
int rtc_peer_connection_create_offer(struct rtc_peer_connection *pc,
				     char *sdp_buf, size_t sdp_len);

/**
 * @brief Generate an SDP answer.
 *
 * Call after rtc_peer_connection_set_remote_description() when the remote
 * side sent an offer.  Populates @p sdp_buf with the local answer SDP.
 *
 * @param pc      Peer connection handle.
 * @param sdp_buf Buffer that receives the SDP answer string.
 * @param sdp_len Size of @p sdp_buf in bytes.
 *
 * @retval 0       Success.
 * @retval -ENOSPC Buffer too small.
 * @retval -EINVAL No remote offer available.
 */
int rtc_peer_connection_create_answer(struct rtc_peer_connection *pc,
				      char *sdp_buf, size_t sdp_len);

/**
 * @brief Apply a local session description to the peer connection.
 *
 * Triggers ICE candidate gathering.  The @p type parameter must be
 * @c "offer" or @c "answer".
 *
 * @param pc   Peer connection handle.
 * @param type SDP type string (@c "offer" or @c "answer").
 * @param sdp  NUL-terminated SDP string.
 *
 * @retval 0       Success.
 * @retval -EINVAL Invalid type or SDP content.
 */
int rtc_peer_connection_set_local_description(struct rtc_peer_connection *pc,
					      const char *type,
					      const char *sdp);

/**
 * @brief Apply a remote session description received via signalling.
 *
 * The @p type parameter must be @c "offer" or @c "answer".
 *
 * @param pc   Peer connection handle.
 * @param type SDP type string (@c "offer" or @c "answer").
 * @param sdp  NUL-terminated SDP string.
 *
 * @retval 0       Success.
 * @retval -EINVAL Invalid type or SDP content.
 */
int rtc_peer_connection_set_remote_description(struct rtc_peer_connection *pc,
					       const char *type,
					       const char *sdp);

/**
 * @brief Add a remote ICE candidate received via signalling.
 *
 * @p candidate is the ICE candidate attribute value string in the SDP
 * @c a=candidate: format (without the @c "a=" prefix or line ending).
 *
 * @param pc        Peer connection handle.
 * @param candidate NUL-terminated candidate attribute value string.
 *
 * @retval 0       Success.
 * @retval -ENOSPC No room to store more remote candidates.
 * @retval -EINVAL Malformed candidate string.
 */
int rtc_peer_connection_add_ice_candidate(struct rtc_peer_connection *pc,
					  const char *candidate);

/* -------------------------------------------------------------------------
 * Data channel API
 * ---------------------------------------------------------------------- */

/**
 * @brief Create a new RTCDataChannel.
 *
 * The channel begins in the @ref RTC_DATA_CHANNEL_CONNECTING state.  It
 * transitions to @ref RTC_DATA_CHANNEL_OPEN once the DTLS handshake
 * succeeds and the remote peer acknowledges the channel open, at which
 * point @p cbs->on_open is invoked.
 *
 * @param pc       Peer connection that owns this channel.
 * @param label    NUL-terminated channel label (at most
 *                 CONFIG_WEBRTC_DATA_CHANNEL_LABEL_LEN characters).
 * @param init     Optional channel initialisation options.  Pass NULL for
 *                 defaults (ordered, reliable, no sub-protocol).
 * @param cbs      Event callbacks.  May be NULL.  Copied internally.
 * @param user_data Opaque value forwarded to every callback.
 * @param[out] dc  Set to the new data channel handle on success.
 *
 * @retval 0       Success.
 * @retval -ENOMEM No free data channel context available.
 * @retval -EINVAL Invalid argument or peer connection in wrong state.
 */
int rtc_data_channel_create(struct rtc_peer_connection *pc,
			    const char *label,
			    const struct rtc_data_channel_init *init,
			    const struct rtc_data_channel_callbacks *cbs,
			    void *user_data,
			    struct rtc_data_channel **dc);

/**
 * @brief Set or replace the callbacks for a data channel.
 *
 * Safe to call at any time, including before the channel is open.
 *
 * @param dc        Data channel handle.
 * @param cbs       New callbacks.  The structure is copied internally.
 * @param user_data Opaque value forwarded to every callback.
 */
void rtc_data_channel_set_callbacks(struct rtc_data_channel *dc,
				    const struct rtc_data_channel_callbacks *cbs,
				    void *user_data);

/**
 * @brief Send a message on a data channel.
 *
 * The channel must be in the @ref RTC_DATA_CHANNEL_OPEN state.
 *
 * @param dc   Data channel handle.
 * @param data Pointer to message payload.
 * @param len  Length of payload in bytes.
 *
 * @retval >=0  Number of bytes accepted (may be less than @p len on a
 *              partial send).
 * @retval -ENOTCONN Channel not open.
 * @retval -errno    Socket-level error.
 */
int rtc_data_channel_send(struct rtc_data_channel *dc,
			  const uint8_t *data, size_t len);

/**
 * @brief Close a data channel.
 *
 * Initiates an orderly close.  The @p cbs->on_close callback will be
 * invoked when the close is complete.  The handle must not be used after
 * @p on_close fires.
 *
 * @param dc Data channel handle.
 */
void rtc_data_channel_close(struct rtc_data_channel *dc);

/**
 * @brief Return the label of a data channel.
 *
 * @param dc Data channel handle.
 *
 * @return Pointer to the NUL-terminated label string (valid for the
 *         lifetime of @p dc).
 */
const char *rtc_data_channel_get_label(const struct rtc_data_channel *dc);

/**
 * @brief Return the current state of a data channel.
 *
 * @param dc Data channel handle.
 *
 * @return Current @ref rtc_data_channel_state.
 */
enum rtc_data_channel_state rtc_data_channel_get_state(
	const struct rtc_data_channel *dc);

/** @cond INTERNAL_HIDDEN */
#if defined(CONFIG_WEBRTC)
void webrtc_init(void);
#else
static inline void webrtc_init(void)
{
}
#endif
/** @endcond */

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_NET_WEBRTC_H_ */
