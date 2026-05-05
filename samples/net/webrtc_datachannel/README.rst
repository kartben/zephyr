.. zephyr:code-sample:: webrtc-datachannel
   :name: WebRTC Data Channel
   :relevant-api: webrtc

   Open a WebRTC peer connection and exchange messages over an
   RTCDataChannel.

Overview
********

This sample application demonstrates how to use the Zephyr WebRTC data
channel library (:kconfig:option:`CONFIG_WEBRTC`) to:

1. Create an :c:type:`rtc_peer_connection` as the **offerer**.
2. Generate an SDP offer and apply it as the local description, which
   triggers ICE host candidate gathering.
3. Receive signalling (SDP answer and ICE candidates) from a remote
   peer over a side-channel (simulated here by a loopback exchange with
   a second peer connection in the same application).
4. Apply the remote description to start ICE connectivity checks.
5. Once connected, open an :c:type:`rtc_data_channel` and exchange
   "Hello, WebRTC!" messages between the two peers.

.. note::

   This sample uses a **loopback exchange** to avoid the need for an
   external signalling server.  In a real deployment the SDP offer/answer
   and ICE candidates would be exchanged via an application-defined
   signalling mechanism (e.g. WebSocket, MQTT, or serial).

.. note::

   The data channel transport uses a Zephyr-specific lightweight
   framing over DTLS rather than the standard SCTP-over-DTLS defined in
   :rfc:`8832`.  Browser interoperability is not supported in this
   initial version.

Requirements
************

- :ref:`networking_with_host`

Building and Running
********************

Build and run the sample on QEMU:

.. zephyr-app-commands::
   :zephyr-app: samples/net/webrtc_datachannel
   :board: qemu_x86
   :goals: build run
   :compact:

Expected output
===============

.. code-block:: console

    [00:00:00.000,000] <inf> net_webrtc_sample: WebRTC data channel sample
    [00:00:00.001,000] <inf> net_webrtc: peer connection created
    [00:00:00.002,000] <inf> net_webrtc: ICE: gathered 1 host candidate(s)
    [00:00:00.003,000] <inf> net_webrtc_sample: Offerer ICE candidate: candidate:1 1 UDP ...
    [00:00:00.004,000] <inf> net_webrtc: peer connection created
    [00:00:00.005,000] <inf> net_webrtc: ICE: gathered 1 host candidate(s)
    [00:00:00.006,000] <inf> net_webrtc: WebRTC: connection established
    [00:00:00.007,000] <inf> net_webrtc_sample: Data channel open
    [00:00:00.008,000] <inf> net_webrtc_sample: Received: Hello, WebRTC!

Configuration
*************

Key configuration options:

.. list-table::
   :header-rows: 1

   * - Kconfig option
     - Description
   * - :kconfig:option:`CONFIG_WEBRTC_MAX_PEER_CONNECTIONS`
     - Number of simultaneous peer connections.
   * - :kconfig:option:`CONFIG_WEBRTC_MAX_DATA_CHANNELS`
     - Maximum data channels per peer connection.
   * - :kconfig:option:`CONFIG_WEBRTC_MAX_ICE_CANDIDATES`
     - Maximum ICE candidates per side.
