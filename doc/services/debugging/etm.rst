.. _etm_trace:

Arm CoreSight ETM instruction trace
###################################

The Arm CoreSight Embedded Trace Macrocell (ETM) is a hardware trace source that
records the executed instruction stream of a CPU. Unlike software trace sources
such as the ITM or STM, the ETM traces the program counter flow itself, which
makes it possible to reconstruct exactly which instructions ran, with very low
runtime overhead.

This API programs an ETMv4 trace source (for example the ETM-M33 found on
Cortex-M33) and an on-chip trace sink, so the instruction stream can be captured
on-target and decoded offline against the application image. No external trace
probe (such as a J-Trace) is required.

Trace path
**********

The ETM emits a formatted trace stream onto the CoreSight ATB (AMBA Trace Bus).
The SoC trace fabric (ATB funnels and replicators, plus the trace power domain)
routes that stream, identified by an ATB trace ID, to a trace sink:

* **ETB** (Embedded Trace Buffer) -- a small on-chip SRAM buffer.
* **ETR** (Embedded Trace Router) -- routes the trace into a system RAM buffer.

Both sinks are Trace Memory Controllers (TMC). The captured data is the raw,
formatted CoreSight byte stream and is meant to be decoded offline.

The trace fabric routing is SoC specific and is configured by the platform
CoreSight driver during system initialization. The ETM trace API itself is
limited to the architecturally defined ETMv4 and TMC programming, so it can be
reused across Cortex-M SoCs.

Configuration
*************

Enable the API with :kconfig:option:`CONFIG_ETM_TRACE`. It requires a devicetree
node with the ``arm,coresight-etm4x`` compatible (node label ``etm``) and a
trace sink with the ``arm,coresight-tmc`` compatible.

Select the sink with :kconfig:option:`CONFIG_ETM_TRACE_SINK_ETB` (node label
``etb``) or :kconfig:option:`CONFIG_ETM_TRACE_SINK_ETR` (node labels ``etr`` and
``etr_buffer``).

Additional trace generation options:

* :kconfig:option:`CONFIG_ETM_TRACE_BRANCH_BROADCAST` -- broadcast all branch
  target addresses so the trace can be decoded without the program image.
* :kconfig:option:`CONFIG_ETM_TRACE_TIMESTAMPS` -- insert global timestamps.
* :kconfig:option:`CONFIG_ETM_TRACE_SYNC_PERIOD` -- synchronization period.

The ATB trace ID is taken from the ``arm,trace-id`` property of the ``etm`` node
and must match the ID the SoC fabric forwards to the selected sink.

Usage
*****

Bracket the code of interest with :c:func:`etm_trace_start` and
:c:func:`etm_trace_stop`, then read the captured bytes with
:c:func:`etm_trace_read`:

.. code-block:: c

   #include <zephyr/debug/coresight/etm.h>

   uint8_t buf[4096];

   etm_trace_start();
   /* ... code to trace ... */
   etm_trace_stop();

   ssize_t len = etm_trace_read(buf, sizeof(buf));

The :zephyr:code-sample:`etm_trace` sample dumps the captured stream on the
console for offline decoding with an ETMv4 decoder such as OpenCSD or ptm2human.

API documentation
*****************

.. doxygengroup:: etm_trace
