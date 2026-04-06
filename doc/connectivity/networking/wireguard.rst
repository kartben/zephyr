.. _wireguard:

WireGuard VPN
#############

Overview
********

WireGuard is a modern, high-performance VPN protocol that uses
state-of-the-art cryptography (Curve25519, ChaCha20-Poly1305, BLAKE2s,
SipHash24) to establish encrypted tunnels between endpoints with minimal
code complexity.  Zephyr 4.4 introduces native WireGuard support, allowing
Zephyr devices to participate in WireGuard VPN networks as a peer.

Key properties of the WireGuard implementation:

- **Stateless** — no connection state machine; sessions are established
  on the first data packet.
- **Identity-based** — each peer is identified by a 256-bit public key
  (Curve25519); there are no usernames, passwords, or certificates.
- **Low overhead** — the handshake is only one round-trip; keepalive
  packets are small and infrequent.
- **Dual-stack** — tunnels carry both IPv4 and IPv6 traffic over a UDP
  transport.

Architecture
************

The Zephyr WireGuard subsystem creates one or more virtual network
interfaces that tunnel all traffic through encrypted WireGuard sessions:

.. code-block:: text

   Application (BSD Sockets)
         │ sends to 198.51.100.2
         ▼
   VPN interface  wg0  (198.51.100.1/24)
         │
         ▼
   WireGuard subsystem
   ┌──────────────────────────────────────────────────┐
   │  Handshake / session management (Noise_IKpsk2)   │
   │  Packet encryption (ChaCha20-Poly1305)           │
   │  Allowed-IP routing table                        │
   │  Keepalive timer                                 │
   └──────────────────────────────────────────────────┘
         │ encrypted UDP datagram (port 51820)
         ▼
   Physical interface  eth0  (192.0.2.1/24)
         │
         ▼
   Peer endpoint  192.0.2.2:51820

A single WireGuard control interface (``wg_ctrl``) anchors all VPN
virtual interfaces.  Each call to :c:func:`wireguard_peer_add` creates a
new ``wg0``/``wg1``/… interface bound to one peer.

Network interfaces
==================

``wg_ctrl``
  The WireGuard control interface.  It carries no IP address and is not
  used directly by applications.  It exists as the parent of all peer
  interfaces.

``wg0``, ``wg1``, …
  Per-peer virtual interfaces returned by :c:func:`wireguard_peer_add`.
  Applications route traffic that should be tunneled through one of
  these interfaces by adding a route or by binding a socket to the
  interface.

Key management
**************

WireGuard uses 256-bit Curve25519 key pairs.  Keys are represented as
32-byte binary values or as base64-encoded strings.

Generating keys
===============

Use the ``wg`` utility on a Linux host:

.. code-block:: bash

   $ wg genkey | tee privatekey | wg pubkey > publickey
   $ cat privatekey    # e.g. lmAIbJR8PQOpgJxmfOydBiDbexTMEKsjglZ1sj3kIjs=
   $ cat publickey     # e.g. v/xiHtIDhnnMJV3SbqI+cChcSqfrU4zlhLUUbL1J8x4=

The private key is configured in Zephyr via
:kconfig:option:`CONFIG_NET_SAMPLE_COMMON_VPN_MY_PRIVATE_KEY`.  The peer's
public key is passed to :c:func:`wireguard_peer_add` via
:c:struct:`wireguard_peer_config`.

.. warning::

   **Never reuse keys.**  The sample configuration files contain
   example keys for documentation purposes only.  Always generate a
   fresh key pair for each deployment.

Pre-shared keys (optional)
===========================

An optional 32-byte pre-shared key (PSK) can be added to the peer
configuration to provide an additional layer of post-quantum security.
Set :c:member:`wireguard_peer_config.preshared_key` to a pointer to the
32-byte key, or to ``NULL`` to skip PSK.

Configuration
*************

Add the following to your project's ``prj.conf``:

.. code-block:: kconfig

   CONFIG_WIREGUARD=y
   CONFIG_NET_SAMPLE_COMMON_VPN_MY_PRIVATE_KEY="<your base64 private key>"
   CONFIG_NET_SAMPLE_COMMON_VPN_PEER_PUBLIC_KEY="<peer base64 public key>"
   CONFIG_NET_SAMPLE_COMMON_VPN_PEER_IP_ADDR="<peer endpoint address>"
   CONFIG_NET_SAMPLE_COMMON_VPN_MY_ADDR="<local VPN IP>"
   CONFIG_NET_SAMPLE_COMMON_VPN_ALLOWED_PEER_ADDR="<allowed CIDR>"

Key Kconfig symbols:

:kconfig:option:`CONFIG_WIREGUARD`
   Enable the WireGuard subsystem.

:kconfig:option:`CONFIG_WIREGUARD_MAX_PEERS`
   Maximum number of simultaneous WireGuard peers.

:kconfig:option:`CONFIG_WIREGUARD_MAX_SRC_IPS`
   Maximum number of allowed-IP entries per peer.

:kconfig:option:`CONFIG_WIREGUARD_KEEPALIVE`
   Keepalive interval in seconds.  A value of 0 disables keepalive.
   A value of 25 is typical for traversing NAT.

:kconfig:option:`CONFIG_WIREGUARD_INTERFACE`
   Name prefix for WireGuard virtual interfaces.

C API
*****

Programmatic access to WireGuard is available through the API defined in
:zephyr_file:`include/zephyr/net/wireguard.h`.

Add a peer
==========

.. code-block:: c

   #include <zephyr/net/wireguard.h>

   static struct wireguard_peer_config peer = {
       .public_key = "v/xiHtIDhnnMJV3SbqI+cChcSqfrU4zlhLUUbL1J8x4=",
       .preshared_key = NULL,
       .keepalive_interval = 25,
   };

   /* Set peer endpoint */
   net_ipaddr_parse("192.0.2.2", strlen("192.0.2.2"),
                    (struct sockaddr *)&peer.endpoint_ip);
   net_sin(&peer.endpoint_ip)->sin_port = htons(51820);

   /* Allow traffic from 198.51.100.0/24 through this peer */
   net_ipaddr_parse("198.51.100.0", strlen("198.51.100.0"),
                    &peer.allowed_ip[0].addr);
   peer.allowed_ip[0].mask_len = 24;
   peer.allowed_ip[0].is_valid = true;

   struct net_if *vpn_iface;
   int peer_id = wireguard_peer_add(&peer, &vpn_iface);
   if (peer_id < 0) {
       printk("wireguard_peer_add failed: %d\n", peer_id);
   }

Remove a peer
=============

.. code-block:: c

   wireguard_peer_remove(peer_id);

Shell commands
**************

When the :ref:`net_shell` is enabled, the ``net wg`` command provides
interactive access:

.. code-block:: console

   uart:~$ net wg setup -k <base64-private-key> -i <iface-index>
   uart:~$ net wg add -k <base64-peer-pubkey> -a <allowed-cidrs> -e <endpoint>
   uart:~$ net wg show

Sample
******

See :zephyr:code-sample:`wireguard-vpn` for a complete working example
using ``native_sim``.  The sample sets up the WireGuard tunnel
automatically and demonstrates an echo-server application communicating
through the VPN.

API Reference
*************

.. doxygengroup:: wg_vpn_service
