.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. zephyr:code-sample:: sockets-sctp-loopback
   :name: SCTP loopback over BSD sockets
   :relevant-api: bsd_sockets

   Exchange SCTP messages over a loopback connection using BSD sockets.

Overview
********

This sample creates an SCTP ``SOCK_SEQPACKET`` listener and client, connects
them over IPv4 loopback, and exchanges one message in each direction.

The source code for this sample can be found at:
:zephyr_file:`samples/net/sockets/sctp_loopback`.

Requirements
************

* A socket implementation with SCTP offload support
* For native simulator builds, a Linux host with SCTP support enabled

Building and Running
********************

Build the sample for the native simulator with the NSOS socket offload backend:

.. zephyr-app-commands::
   :zephyr-app: samples/net/sockets/sctp_loopback
   :board: native_sim
   :conf: overlay-nsos.conf
   :goals: build
   :compact:

Run the resulting executable:

.. code-block:: console

   west build -t run

If the selected socket backend does not support SCTP, the sample prints an
informative message and exits without exchanging data.
