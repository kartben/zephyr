.. _securevault_node:

SecureVault Node — Zephyr 4.4 Feature Showcase
###############################################

Overview
********

**SecureVault Node** is a complete, runnable demo application that showcases six
new features introduced in Zephyr 4.4 through a single coherent business scenario.

**Business scenario:** A compact IoT node deployed in sensitive facilities
(data centres, pharma labs, industrial control rooms).  Each node:

* Authenticates field engineers via fingerprint before allowing local
  reconfiguration.
* Continuously monitors temperature and humidity.
* Transmits telemetry over an encrypted WireGuard VPN tunnel to the facility
  management system.
* Adapts its CPU clock speed to the current workload using the scheduler
  pressure policy.
* Exposes an interactive shell for technicians.

Zephyr 4.4 Features Demonstrated
*********************************

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Feature
     - Source file
     - What it shows
   * - **Biometrics API** *(new driver class)*
     - ``src/auth.c``
     - Uniform enroll → capture → finalize workflow across fingerprint,
       iris, and face backends.  Uses the in-tree emulated driver on
       ``native_sim``.
   * - **Scope-based Cleanup** *(RAII in C)*
     - ``src/auth.c``, ``src/report.c``
     - ``scope_guard(k_mutex)`` and ``scope_defer(biometric_enroll_abort)``
       eliminate error-path ``goto`` ladders.  The "Before / After"
       comment in ``auth.c`` shows the contrast directly.
   * - **WireGuard VPN** *(experimental)*
     - ``src/report.c``
     - Full VPN stack on bare metal. Telemetry is sent over a standard
       UDP socket; encryption is handled transparently by the WireGuard
       virtual interface.
   * - **CPU Frequency Pressure Policy** *(experimental)*
     - ``src/monitor.c``
     - Three sensor-pipeline threads at priorities 1, 5, and 10 produce
       varying scheduler pressure.  The policy scales the CPU P-state
       without any direct ``cpu_freq_*`` calls from application code.
   * - **shell_readline()**
     - ``src/shell.c``
     - Blocking interactive input inside a shell command handler.  Used
       for enrollment and wipe confirmation prompts.
   * - **ztest Benchmark Framework**
     - ``tests/benchmark/src/main.c``
     - Cycle-accurate statistical benchmarking of biometric operations.
       Reports mean, stddev, min, and max latency.

Requirements
************

* Zephyr SDK 1.0 or later (ships with Zephyr 4.4).
* For WireGuard: the ``zephyr-wireguard`` west module must be present in
  your workspace (add it to ``west.yml``).  Without it the reporting
  module falls back to a log-only stub.
* ``native_sim`` board for simulation; ``nrf5340dk/nrf5340/cpuapp`` or any
  board with Ethernet for the full WireGuard path.

Build and Run
*************

Simulation (native_sim — no hardware required)
===============================================

.. code-block:: console

   # Without WireGuard (works out of the box):
   west build -b native_sim samples/demo/zephyr44_showcase -- -DCONFIG_WIREGUARD=n
   west build -t run

   # With WireGuard (requires zephyr-wireguard module):
   west build -b native_sim samples/demo/zephyr44_showcase
   west build -t run

Once running, interact via the shell::

   uart:~$ securevault status
   uart:~$ securevault enroll 1
   uart:~$ securevault verify 1
   uart:~$ securevault identify
   uart:~$ securevault alarm
   uart:~$ securevault wipe

Benchmarks
==========

.. code-block:: console

   west build -b native_sim samples/demo/zephyr44_showcase/tests/benchmark
   west build -t run

The benchmark suite measures biometric operation latency and prints a
statistical summary table (mean, stddev, min, max in CPU cycles).

Real Hardware (nRF5340 DK)
==========================

.. code-block:: console

   west build -b nrf5340dk/nrf5340/cpuapp samples/demo/zephyr44_showcase
   west flash

For the fingerprint sensor add a board-specific overlay that points the
``fingerprint`` DT alias at your hardware driver node (e.g. ZFM-x0 or GT5x).

Demo Script
***********

See ``DEMO_SCRIPT.md`` for a step-by-step presenter guide covering the
full ~18-minute live demo flow.

Configuration
*************

.. csv-table::
   :header: "Kconfig symbol", "Default", "Purpose"

   ``CONFIG_BIOMETRICS``, ``y``, "Enable biometrics driver subsystem (4.4)"
   ``CONFIG_SCOPE_CLEANUP_HELPERS``, ``y``, "Enable RAII cleanup macros (4.4)"
   ``CONFIG_CPU_FREQ``, ``y``, "Enable CPU frequency subsystem (4.4)"
   ``CONFIG_CPU_FREQ_POLICY_PRESSURE``, ``y``, "Pressure-based policy (4.4)"
   ``CONFIG_CPU_FREQ_PSTATE_SET_STUB``, ``y``, "Stub P-state setter for portability"
   ``CONFIG_WIREGUARD``, *(disabled)*", "WireGuard VPN (4.4 experimental)"
   ``CONFIG_SHELL``, ``y``, "Zephyr shell"
