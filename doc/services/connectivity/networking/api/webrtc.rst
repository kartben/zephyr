.. _webrtc_interface:

WebRTC Data Channel API
#######################

.. contents::
    :local:
    :depth: 2

Overview
********

The WebRTC data channel library allows Zephyr-based devices to open
peer-to-peer data channels with other WebRTC-capable nodes.  It implements
the key building blocks of the W3C `RTCPeerConnection`_ and
`RTCDataChannel`_ APIs:

- **SDP offer/answer** — generates and parses the minimal Session
  Description Protocol (:rfc:`4566`) subset needed for data channel
  negotiation.
- **ICE host candidates** — gathers host candidates from local network
  interfaces and performs STUN (:rfc:`5389`) binding-request connectivity
  checks against remote candidates.
- **DTLS transport** — reuses Zephyr's existing DTLS socket support
  (:kconfig:option:`CONFIG_NET_SOCKETS_ENABLE_DTLS`) to provide the secure
  transport layer.
- **Data channel framing** — a lightweight 4-byte header framing protocol
  that runs over the DTLS connection to multiplex named data channels.

Enable the library with:

.. code-block:: kconfig

   CONFIG_WEBRTC=y

.. note::

   WebRTC support is currently **EXPERIMENTAL** in Zephyr.  APIs and Kconfig
   options may change between releases.

.. warning::

   The data channel transport uses a **Zephyr-specific** lightweight framing
   over DTLS rather than the standard SCTP-over-DTLS defined in :rfc:`8832`.
   Interoperability with browsers or standard W3C WebRTC endpoints is
   **not** supported in this initial version.  Server-reflexive (STUN) and
   relay (TURN) ICE candidates are also not gathered.


Architecture
************

The library is organised into four layers that sit on top of Zephyr's
standard UDP and DTLS socket primitives:

.. code-block:: none

   Application
       │
       │  RTCPeerConnection / RTCDataChannel API (webrtc.c)
       │
       ├─ ICE agent: host candidate gathering + STUN checks (webrtc_ice.c)
       │       └── UDP sockets (zsock_socket / zsock_sendto)
       │
       ├─ DTLS transport (webrtc_dtls.c)
       │       └── DTLS sockets (IPPROTO_DTLS_1_2)
       │
       └─ Data channel framing (webrtc_datachannel.c)
               └── DTLS socket (zsock_sendmsg / zsock_recv)

Peer Connection States
======================

.. code-block:: none

   NEW
    │  set_local_description("offer")
    ▼
   HAVE_LOCAL_OFFER ──── set_remote_description("answer") ──► GATHERING
    │                                                              │
    │  set_remote_description("offer")              ICE done      │
    │                                                              ▼
   HAVE_REMOTE_OFFER ─── set_local_description("answer") ──►  CHECKING
    │                                                              │
    │                                               DTLS done     │
    │                                                              ▼
    │                                                         CONNECTED
    │
    └── error / close() ──► FAILED / CLOSED


API Usage
*********

Registering DTLS Credentials
=============================

The peer connection uses DTLS for transport security.  Before creating a
peer connection, register a certificate and private key via the TLS
credentials API:

.. code-block:: c

   #include <zephyr/net/tls_credentials.h>

   #define MY_DTLS_TAG 1

   tls_credential_add(MY_DTLS_TAG,
                      TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
                      my_cert_der, sizeof(my_cert_der));
   tls_credential_add(MY_DTLS_TAG,
                      TLS_CREDENTIAL_PRIVATE_KEY,
                      my_key_der, sizeof(my_key_der));

Creating a Peer Connection
==========================

.. code-block:: c

   #include <zephyr/net/webrtc.h>

   static void on_ice_candidate(struct rtc_peer_connection *pc,
                                const char *candidate,
                                void *user_data)
   {
       if (candidate != NULL) {
           /* Send candidate to remote via signalling channel */
       }
   }

   static void on_connection_state_change(struct rtc_peer_connection *pc,
                                          enum rtc_peer_connection_state s,
                                          void *user_data)
   {
       if (s == RTC_PEER_CONNECTION_CONNECTED) {
           /* Ready to open data channels */
       }
   }

   static const struct rtc_peer_connection_callbacks my_cbs = {
       .on_ice_candidate         = on_ice_candidate,
       .on_connection_state_change = on_connection_state_change,
   };

   struct rtc_configuration cfg = {
       .dtls_tag = MY_DTLS_TAG,
   };
   struct rtc_peer_connection *pc;
   rtc_peer_connection_create(&cfg, &my_cbs, NULL, &pc);

Offer/Answer Handshake (Offerer Side)
======================================

.. code-block:: c

   char offer_sdp[1024];

   /* Generate and apply the local description. */
   rtc_peer_connection_create_offer(pc, offer_sdp, sizeof(offer_sdp));
   rtc_peer_connection_set_local_description(pc, "offer", offer_sdp);

   /* Send offer_sdp to the remote peer via out-of-band signalling. */
   my_signal_send("offer", offer_sdp);

   /* When the answer arrives: */
   rtc_peer_connection_set_remote_description(pc, "answer", received_answer);

   /* Add each received ICE candidate from the remote peer: */
   rtc_peer_connection_add_ice_candidate(pc, received_candidate);

Offer/Answer Handshake (Answerer Side)
=======================================

.. code-block:: c

   /* When the offer arrives: */
   rtc_peer_connection_set_remote_description(pc, "offer", received_offer);

   char answer_sdp[1024];
   rtc_peer_connection_create_answer(pc, answer_sdp, sizeof(answer_sdp));
   rtc_peer_connection_set_local_description(pc, "answer", answer_sdp);

   /* Send answer_sdp back via out-of-band signalling. */
   my_signal_send("answer", answer_sdp);

Opening a Data Channel
======================

.. code-block:: c

   static void on_dc_open(struct rtc_data_channel *dc, void *user_data)
   {
       const uint8_t hello[] = "Hello, WebRTC!";
       rtc_data_channel_send(dc, hello, sizeof(hello) - 1);
   }

   static void on_dc_message(struct rtc_data_channel *dc,
                              const uint8_t *data, size_t len,
                              void *user_data)
   {
       printk("Received: %.*s\n", (int)len, data);
   }

   static const struct rtc_data_channel_callbacks dc_cbs = {
       .on_open    = on_dc_open,
       .on_message = on_dc_message,
   };

   struct rtc_data_channel *dc;
   /* Call after RTC_PEER_CONNECTION_CONNECTED: */
   rtc_data_channel_create(pc, "my-channel", NULL, &dc_cbs, NULL, &dc);


Signalling
**********

WebRTC requires an out-of-band signalling channel to exchange SDP
descriptions and ICE candidates.  The library is agnostic about the
signalling transport.  Typical options for Zephyr-based devices include:

- **WebSocket** (using :kconfig:option:`CONFIG_WEBSOCKET_CLIENT`)
- **MQTT** (using :kconfig:option:`CONFIG_MQTT_LIB`)
- **Serial / UART**
- **Custom application protocol**

See :zephyr:code-sample:`webrtc-datachannel` for a self-contained loopback
example that simulates signalling by exchanging SDP and candidates directly
between two peer connections running on the same device.


Configuration Reference
***********************

.. list-table::
   :header-rows: 1

   * - Kconfig option
     - Default
     - Description
   * - :kconfig:option:`CONFIG_WEBRTC`
     - ``n``
     - Enable the WebRTC data channel library.
   * - :kconfig:option:`CONFIG_WEBRTC_MAX_PEER_CONNECTIONS`
     - ``1``
     - Number of simultaneous peer connection contexts.
   * - :kconfig:option:`CONFIG_WEBRTC_MAX_DATA_CHANNELS`
     - ``4``
     - Maximum data channels per peer connection.
   * - :kconfig:option:`CONFIG_WEBRTC_MAX_ICE_CANDIDATES`
     - ``4``
     - Maximum ICE candidates stored per side.
   * - :kconfig:option:`CONFIG_WEBRTC_DATA_CHANNEL_LABEL_LEN`
     - ``64``
     - Maximum data channel label length (characters).
   * - :kconfig:option:`CONFIG_WEBRTC_SDP_MAX_LEN`
     - ``1024``
     - SDP buffer size in bytes.
   * - :kconfig:option:`CONFIG_WEBRTC_IO_RECV_BUF_SIZE`
     - ``1500``
     - Internal receive buffer size in bytes.
   * - :kconfig:option:`CONFIG_WEBRTC_IO_POLL_INTERVAL_MS`
     - ``20``
     - Background I/O polling interval in milliseconds.

Limitations
***********

The following limitations apply to this initial experimental release:

- **SCTP not implemented**: The data channel wire protocol is a
  Zephyr-specific lightweight framing over DTLS.  Standard WebRTC
  SCTP-over-DTLS (:rfc:`8832`) is not supported, so interoperability with
  browsers is not possible without a future SCTP upgrade.
- **Host candidates only**: ICE candidate gathering produces only host
  candidates (local interface addresses).  Server-reflexive (STUN) and
  relay (TURN) candidates are not gathered.
- **No media**: Audio/video tracks, codecs, and RTP are not included in
  this release.  Only :c:type:`rtc_data_channel` is supported.
- **Single m-section**: The SDP generated by the library contains a single
  ``application`` media section.  Multi-media SDP is not generated.
- **No ICE restart**: Calling :c:func:`rtc_peer_connection_set_local_description`
  a second time after connection is not supported.


API Reference
*************

.. doxygengroup:: webrtc

.. _RTCPeerConnection: https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection
.. _RTCDataChannel: https://www.w3.org/TR/webrtc/#dom-rtcdatachannel
