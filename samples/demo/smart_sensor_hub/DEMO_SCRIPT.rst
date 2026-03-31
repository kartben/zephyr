.. _smart_sensor_hub:

Smart Sensor Hub — Zephyr 4.4 Feature Showcase
###############################################

.. contents::
   :local:
   :depth: 2

Overview
********

This demo presents a **Smart Industrial Environment Monitor** — a
factory-floor sensor hub that continuously reads temperature, humidity,
and pressure, evaluates safety thresholds, and dispatches alerts.

It is designed as a self-contained showcase of three headline features
introduced in **Zephyr 4.4**, woven into a realistic IoT application
rather than isolated API samples.

Business Scenario
=================

A manufacturing plant requires continuous environmental monitoring on its
production floor.  Sensor stations read ambient conditions every two seconds,
evaluate them against configurable safety thresholds, and raise alerts that
can drive actuators, trigger notifications, or log compliance data.

The firmware must be **reliable** (no resource leaks even on error paths),
**modular** (sensor acquisition, alert logic, and reporting are independent
modules), and **efficient** (message passing has minimal overhead).

Zephyr 4.4 Features Demonstrated
=================================

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Feature
     - Where it appears
   * - **Zbus pub/sub messaging**
     - Sensor readings flow from ``sensor_sim`` to ``alert_engine`` via
       ``sensor_data_chan``; alerts are dispatched on ``alert_chan``.
       Channels, subscribers and listeners are wired at compile time with
       zero dynamic allocation.
   * - **Scope-based cleanup helpers**
     - ``sensor_sim.c`` uses ``scope_var`` to allocate a calibration context
       that is **automatically freed** when the function returns — even on
       early-return error paths.  ``alert_engine.c`` uses ``scope_guard`` to
       lock/unlock a statistics mutex with guaranteed release.
   * - **Ztest Benchmark Framework**
     - The companion ``benchmark/`` application measures the cycle cost of
       zbus publish, zbus read, scope-var alloc/free, and sustained message
       throughput using the new ``ZTEST_BENCHMARK`` macros.

Architecture
************

.. code-block:: text

   ┌──────────────┐       zbus        ┌──────────────────┐
   │  sensor_sim  │──────────────────►│   alert_engine   │
   │              │  sensor_data_chan  │                  │
   │  (scope_var  │                   │  (scope_guard    │
   │   cleanup)   │                   │   mutex safety)  │
   └──────────────┘                   └────────┬─────────┘
                                               │
                                          alert_chan
                                               │
                                               ▼
                                      ┌────────────────┐
                                      │ alert_listener │
                                      │   (logging)    │
                                      └────────────────┘

   ┌────────────────────────────────────────────────────────────┐
   │  benchmark/ — Ztest Benchmark Framework                   │
   │  Measures cycle cost of zbus pub, read, scope_var, etc.   │
   └────────────────────────────────────────────────────────────┘

Requirements
************

* Zephyr SDK (any host platform)
* No physical hardware needed — runs on ``native_sim``

Building and Running
********************

Main application
================

.. code-block:: console

   west build -b native_sim samples/demo/smart_sensor_hub
   west build -t run

Expected output (abbreviated):

.. code-block:: console

   [main] Smart Sensor Hub — Zephyr 4.4 Feature Showcase started
   [main] Features demonstrated:
   [main]   [1] Zbus pub/sub messaging (sensor -> alert pipeline)
   [main]   [2] Scope-based cleanup helpers (scope_var, scope_guard)
   [main]   [3] Ztest benchmark framework (see benchmark/ sub-app)
   [sensor_sim] Sensor simulation module initialised
   [alert_engine] Alert engine module initialised
   [sensor_sim] Sensor [#0] T=22.000°C  H=52.500°C  P=101.325 kPa
   [sensor_sim] Sensor [#5] T=26.000°C  H=57.500°C  P=101.575 kPa
   [alert_engine] ALERT [WARNING]  Temperature 26.000°C >= 26.000°C
   [alert_engine] ALERT [WARNING]  Humidity 57.500% >= 55.000%
   ...

The sensor simulation generates a deterministic triangular wave that
deliberately crosses the warning and critical thresholds, so you will see
alerts appear and subside in a repeating pattern.

Benchmark application
=====================

The benchmark application showcases the Ztest Benchmark Framework API.
Because the framework relies on hardware cycle counters and a
non-simulated scheduler, it is designed for real hardware boards (e.g.
``frdm_k64f``).

.. code-block:: console

   west build -b frdm_k64f samples/demo/smart_sensor_hub/benchmark
   west flash

On ``native_sim`` the timed control calibration blocks, so the benchmark
is a **code-reading exercise** — walk the audience through
``benchmark/src/main.c`` to show the API surface:

.. code-block:: console

   Benchmark complete
   ...
   struct_copy            :  mean=XX cycles, min=XX, max=XX (1000 samples)
   zbus_publish           :  mean=XX cycles, min=XX, max=XX (1000 samples)
   zbus_read              :  mean=XX cycles, min=XX, max=XX (1000 samples)
   scope_var_alloc_free   :  mean=XX cycles, min=XX, max=XX (1000 samples)


Demo Script / Storyboard
*************************

This section is a presenter's guide for walking an audience through the
demo.  Each act is self-contained and takes roughly 2–3 minutes.

Act 1 — The Problem (30 seconds)
=================================

*"Imagine you're building firmware for an environmental monitoring station
on a factory floor.  You need modular sensor acquisition, real-time
threshold monitoring, safe resource handling, and you want to prove your
message-passing overhead is low."*

Show the architecture diagram above.

Act 2 — Zbus: Decoupled Messaging (2 minutes)
==============================================

1. Open ``src/channels.h`` — show the message structs and channel
   declarations.
2. Open ``src/main.c`` — show ``ZBUS_CHAN_DEFINE`` for both channels.
3. Open ``src/sensor_sim.c`` — show ``zbus_chan_pub`` publishing a reading.
4. Open ``src/alert_engine.c`` — show ``ZBUS_SUBSCRIBER_DEFINE`` +
   ``zbus_sub_wait`` consuming the same channel.
5. Point out: **zero dynamic allocation, compile-time wiring, type-safe
   messages**.

*"Modules don't know about each other.  The sensor publishes; the alert
engine subscribes.  Adding a new consumer — say a cloud uplink — is one
more ZBUS_CHAN_ADD_OBS line."*

Act 3 — Scope-Based Cleanup: No More Leaks (2 minutes)
=======================================================

1. Open ``src/sensor_sim.c`` — show the ``SCOPE_VAR_DEFINE`` macro and
   the ``scope_var(cal, ctx)(0, 0, 0)`` call inside
   ``sensor_read_cycle``.

   *"This allocates a calibration context from the heap.  When the function
   returns — normally or early — the destructor runs automatically.  It's
   RAII for C."*

2. Open ``src/alert_engine.c`` — show ``SCOPE_GUARD_DEFINE`` for the
   mutex and ``scope_guard(k_mutex)(&stats_mutex)`` in ``record_alert``.

   *"Same pattern for locks.  The mutex is always released.  No forgotten
   unlock on an error branch."*

3. Build and run; point out the ``[scope cleanup] freeing calibration
   context`` debug log (enable ``CONFIG_LOG_DEFAULT_LEVEL=4`` to see it).

Act 4 — Ztest Benchmarks: Prove It's Fast (2 minutes)
======================================================

1. Open ``benchmark/src/main.c`` — walk through
   ``ZTEST_BENCHMARK_SUITE``, ``ZTEST_BENCHMARK``, and the
   ``scope_var_alloc_free`` benchmark.

   *"Zephyr 4.4 adds a first-class benchmarking framework.  Define a
   suite, write tiny functions, and get cycle-accurate stats with mean,
   min, max and standard deviation — all automated."*

2. If running on real hardware, build and flash the benchmark application.
   On ``native_sim``, show the code and explain the API.
3. Highlight the ``zbus_publish`` vs ``struct_copy`` numbers.

   *"Zbus adds minimal overhead on top of a raw memcpy.  That's the power
   of a zero-allocation, compile-time message bus."*

Act 5 — Wrap-Up (30 seconds)
=============================

*"In under 300 lines of application code, we built a real monitoring
station that's modular, leak-proof, and benchmarked.  Scope-based
cleanup, zbus and the benchmark framework are all shipping in Zephyr 4.4
— ready for your next product."*

References
**********

* :ref:`zbus` — Zephyr Bus documentation
* :ref:`cleanup_interface` — Scope-based cleanup helpers
* :ref:`ztest_benchmark` — Ztest Benchmark Framework
* :ref:`cpu_freq` — CPU Frequency Scaling (complementary 4.4 feature)
