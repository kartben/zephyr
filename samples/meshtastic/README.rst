.. _meshtastic-sample:

Meshtastic
##########

Overview
********

This sample demonstrates the Zephyr Meshtastic subsystem.  It initialises
the radio on the default ``LongFast`` channel, registers a receive callback
that logs incoming text messages, and broadcasts ``"Hello from Zephyr!"``
every 30 seconds.

The stack is wire-compatible with the official `Meshtastic
<https://meshtastic.org>`_ firmware: packets are exchanged at SF 11,
BW 250 kHz, CR 4/5, using the same 16-byte packet header, the same
AES-128-CTR encryption scheme with the default channel key, and the same
nanopb-encoded ``Data`` protobuf payload format.

Requirements
************

* A board with a LoRa transceiver supported by the Zephyr ``lora`` driver
  API (e.g. any board with an SX1276 or SX1262 radio) that exposes the
  device via the ``lora0`` devicetree alias.
* The ``nanopb`` module must be present in the west workspace
  (``west update`` with the Zephyr manifest will fetch it).

Building and Running
********************

By default the node ID is derived from the hardware device ID (``HWINFO``).
To use a fixed ID instead, enable custom source and set the default:

.. code-block:: console

   west build -b <your_board> samples/meshtastic -- \
     -DCONFIG_MESHTASTIC_NODE_ID_CUSTOM=y \
     -DCONFIG_MESHTASTIC_NODE_ID_DEFAULT=0x01020304

The sample can also be built for the LoRa radio emulator on ``native_sim``
by adding the appropriate overlay.

Configuration
*************

Node ID options (subsystem Kconfig):

.. option:: CONFIG_MESHTASTIC_NODE_ID_SOURCE

   Choice of how the local node ID is determined: ``HWINFO`` (default when
   available), or ``CUSTOM``.  Bluetooth and Wi-Fi MAC sources are reserved
   for future use.

.. option:: CONFIG_MESHTASTIC_NODE_ID_DEFAULT

   Fixed node ID (hex) when ``CONFIG_MESHTASTIC_NODE_ID_CUSTOM`` is selected
   and the application passes zero in ``meshtastic_config.node_id``.
   Default: ``0xDEADBEEF``.

For a full list of subsystem options see :ref:`meshtastic`.

MQTT gateway (optional)
***********************

On a board with IPv4 networking, enable the native MQTT bridge::

  west build -b <your_board> samples/meshtastic -- -DEXTRA_CONF_FILE=overlay-mqtt.conf

See :ref:`meshtastic_subsys` for topic layout and broker defaults.

Shell commands (optional)
*************************

Enable the subsystem shell with ``overlay-shell.conf`` (``CONFIG_SHELL=y``,
``CONFIG_MESHTASTIC_SHELL=y``). Useful commands:

* ``meshtastic status`` — node counters, primary channel hash, device role, rebroadcast mode
* ``meshtastic channel list`` / ``channel show <0-7>`` — channel table
* ``meshtastic channel set <index> name <str> role secondary psk default`` — runtime channel edit (RAM only)
* ``meshtastic channel disable <index>`` — disable a slot
* ``meshtastic device role [client|router|...]`` — mesh device role
* ``meshtastic device rebroadcast [all|none|local_only|...]`` — relay policy
* ``meshtastic text send [-c <index>] [dest|broadcast] <message>`` — send on a specific channel

Sample Output
*************

.. code-block:: console

   [00:00:00.000] <inf> meshtastic: Meshtastic init: node=0xdeadbeef ch_hash=0x08 freq=906875000Hz
   [00:00:00.001] <inf> meshtastic_sample: Meshtastic sample started, node ID 0xdeadbeef
   [00:00:02.345] <inf> meshtastic_sample: MSG from 0xc0ffee42: Hello mesh!  (RSSI -87 dBm, SNR 7)
   [00:00:30.001] <inf> meshtastic: TX to 0xffffffff port=1 len=29
   [00:00:30.001] <inf> meshtastic_sample: Broadcast sent
