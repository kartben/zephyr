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

Set ``CONFIG_MESHTASTIC_SAMPLE_NODE_ID`` to a unique 32-bit value for your
device.  In production you should derive this from the last four bytes of
the hardware MAC address.

.. code-block:: console

   west build -b <your_board> samples/meshtastic -- \
     -DCONFIG_MESHTASTIC_SAMPLE_NODE_ID=0x01020304

The sample can also be built for the LoRa radio emulator on ``native_sim``
by adding the appropriate overlay.

Configuration
*************

The following Kconfig options customise the subsystem:

.. option:: CONFIG_MESHTASTIC_SAMPLE_NODE_ID

   Local node ID (hex).  Default: ``0xDEADBEEF``.

For a full list of subsystem options see :ref:`meshtastic`.

Sample Output
*************

.. code-block:: console

   [00:00:00.000] <inf> meshtastic: Meshtastic init: node=0xdeadbeef ch_hash=0x08 freq=906875000Hz
   [00:00:00.001] <inf> meshtastic_sample: Meshtastic sample started, node ID 0xdeadbeef
   [00:00:02.345] <inf> meshtastic_sample: MSG from 0xc0ffee42: Hello mesh!  (RSSI -87 dBm, SNR 7)
   [00:00:30.001] <inf> meshtastic: TX to 0xffffffff port=1 len=29
   [00:00:30.001] <inf> meshtastic_sample: Broadcast sent
