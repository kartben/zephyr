.. zephyr:code-sample:: etm_trace
   :name: ETM instruction trace

   Capture Arm CoreSight ETM instruction trace into an on-chip buffer and dump
   it for offline decoding.

Overview
********

This sample shows how to use the :ref:`Arm CoreSight ETM instruction trace
<etm_trace>` API to capture the executed instruction stream of the CPU into an
on-chip trace buffer (ETB), without an external trace probe.

The source code shows how to:

#. Start instruction tracing with :c:func:`etm_trace_start`.
#. Run a small, deterministic workload.
#. Stop tracing with :c:func:`etm_trace_stop`.
#. Read the captured trace bytes with :c:func:`etm_trace_read` and dump them on
   the console as hex for offline decoding.

Requirements
************

Your board must expose an Arm CoreSight ETM (compatible
``arm,coresight-etm4x``) and an on-chip trace sink (compatible
``arm,coresight-tmc``) reachable through the SoC CoreSight trace fabric. This
sample targets the :zephyr:board:`nrf54h20dk` application core, whose
devicetree overlay enables the CoreSight subsystem in ``etm-etb`` mode.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/debug/etm_trace
   :board: nrf54h20dk/nrf54h20/cpuapp
   :goals: build flash
   :compact:

After flashing, the sample prints the workload result followed by the captured
trace, delimited by markers:

.. code-block:: console

   Arm CoreSight ETM instruction trace sample
   Workload result: 144
   Captured 512 bytes of ETM trace:
   ---8<--- ETM TRACE BEGIN ---8<---
   00080100...
   ---8<--- ETM TRACE END ---8<---

Decoding the trace
==================

The dumped bytes are the raw, formatted CoreSight trace stream. Capture the
block between the markers from the console, convert it to a binary file and
decode it offline against the application ELF
(:file:`build/zephyr/zephyr.elf`) with an ETMv4 decoder such as OpenCSD
(``trc_pkt_lister``) or ptm2human.
