.. _introducing_zephyr:

Introduction
############

Zephyr is an open source real-time operating system (RTOS) for resource-constrained
devices. It is designed to be modular, secure, and portable across a wide range of
hardware --- from tiny sensors with just a few kilobytes of memory to complex
multi-core IoT gateways.

Zephyr is hosted by the `Linux Foundation`_ and licensed under the
`Apache 2.0 license`_, with some imported components using other licenses
(see :ref:`zephyr_licensing`).

.. _Linux Foundation: https://www.linuxfoundation.org/
.. _Apache 2.0 license:
   https://github.com/zephyrproject-rtos/zephyr/blob/main/LICENSE

High-Level Architecture
***********************

The following diagram shows how Zephyr's components fit together, from
the application layer down to the hardware, along with the development tools
that support the workflow.

.. raw:: html

   <div style="max-width:820px;margin:1.5em auto;font-family:sans-serif;font-size:13px;line-height:1.3">
     <!-- Development Tools (top bar) -->
     <div style="display:flex;gap:6px;margin-bottom:6px;flex-wrap:wrap">
       <div style="flex:1;min-width:120px;border:2px solid #5b7f95;border-radius:6px;padding:8px 10px;background:#eaf2f8;text-align:center">
         <div style="font-weight:bold;color:#2c3e50;margin-bottom:2px">west</div>
         <div style="font-size:11px;color:#555">Meta-tool: build, flash,<br>debug, workspace mgmt</div>
       </div>
       <div style="flex:1;min-width:120px;border:2px solid #5b7f95;border-radius:6px;padding:8px 10px;background:#eaf2f8;text-align:center">
         <div style="font-weight:bold;color:#2c3e50;margin-bottom:2px">Zephyr SDK</div>
         <div style="font-size:11px;color:#555">Toolchains, QEMU,<br>OpenOCD</div>
       </div>
       <div style="flex:1;min-width:120px;border:2px solid #5b7f95;border-radius:6px;padding:8px 10px;background:#eaf2f8;text-align:center">
         <div style="font-weight:bold;color:#2c3e50;margin-bottom:2px">Twister</div>
         <div style="font-size:11px;color:#555">Test runner &amp;<br>CI framework</div>
       </div>
       <div style="flex:1;min-width:120px;border:2px solid #5b7f95;border-radius:6px;padding:8px 10px;background:#eaf2f8;text-align:center">
         <div style="font-weight:bold;color:#2c3e50;margin-bottom:2px">Build System</div>
         <div style="font-size:11px;color:#555">CMake, Kconfig,<br>Devicetree</div>
       </div>
     </div>
     <!-- Main stack -->
     <div style="border:2px solid #888;border-radius:8px;overflow:hidden">
       <!-- Application -->
       <div style="background:#d5e8d4;padding:10px 14px;border-bottom:2px solid #888;text-align:center">
         <div style="font-weight:bold;color:#2c3e50;font-size:14px">Your Application</div>
         <div style="font-size:11px;color:#555">Application code, prj.conf (Kconfig), devicetree overlays</div>
       </div>
       <!-- OS Services -->
       <div style="background:#fff2cc;padding:10px 14px;border-bottom:2px solid #888">
         <div style="font-weight:bold;color:#2c3e50;text-align:center;margin-bottom:6px;font-size:14px">OS Services &amp; Protocol Stacks</div>
         <div style="display:flex;flex-wrap:wrap;gap:4px;justify-content:center">
           <span style="background:#ffe599;border:1px solid #d4a017;border-radius:4px;padding:3px 8px;font-size:11px">Networking (IP, LwM2M, MQTT, CoAP)</span>
           <span style="background:#ffe599;border:1px solid #d4a017;border-radius:4px;padding:3px 8px;font-size:11px">Bluetooth (Host, Mesh)</span>
           <span style="background:#ffe599;border:1px solid #d4a017;border-radius:4px;padding:3px 8px;font-size:11px">USB</span>
           <span style="background:#ffe599;border:1px solid #d4a017;border-radius:4px;padding:3px 8px;font-size:11px">File Systems</span>
           <span style="background:#ffe599;border:1px solid #d4a017;border-radius:4px;padding:3px 8px;font-size:11px">Logging</span>
           <span style="background:#ffe599;border:1px solid #d4a017;border-radius:4px;padding:3px 8px;font-size:11px">Shell</span>
           <span style="background:#ffe599;border:1px solid #d4a017;border-radius:4px;padding:3px 8px;font-size:11px">Device Mgmt</span>
           <span style="background:#ffe599;border:1px solid #d4a017;border-radius:4px;padding:3px 8px;font-size:11px">Power Mgmt</span>
           <span style="background:#ffe599;border:1px solid #d4a017;border-radius:4px;padding:3px 8px;font-size:11px">POSIX</span>
           <span style="background:#ffe599;border:1px solid #d4a017;border-radius:4px;padding:3px 8px;font-size:11px">Crypto</span>
         </div>
       </div>
       <!-- Kernel -->
       <div style="background:#dae8fc;padding:10px 14px;border-bottom:2px solid #888">
         <div style="font-weight:bold;color:#2c3e50;text-align:center;margin-bottom:6px;font-size:14px">Kernel</div>
         <div style="display:flex;flex-wrap:wrap;gap:4px;justify-content:center">
           <span style="background:#b4d7ff;border:1px solid #6c8ebf;border-radius:4px;padding:3px 8px;font-size:11px">Threads &amp; Scheduling</span>
           <span style="background:#b4d7ff;border:1px solid #6c8ebf;border-radius:4px;padding:3px 8px;font-size:11px">Synchronization (Mutexes, Semaphores)</span>
           <span style="background:#b4d7ff;border:1px solid #6c8ebf;border-radius:4px;padding:3px 8px;font-size:11px">Data Passing (Queues, Pipes)</span>
           <span style="background:#b4d7ff;border:1px solid #6c8ebf;border-radius:4px;padding:3px 8px;font-size:11px">Memory Management</span>
           <span style="background:#b4d7ff;border:1px solid #6c8ebf;border-radius:4px;padding:3px 8px;font-size:11px">Timers &amp; Clocks</span>
           <span style="background:#b4d7ff;border:1px solid #6c8ebf;border-radius:4px;padding:3px 8px;font-size:11px">Interrupt Handling</span>
           <span style="background:#b4d7ff;border:1px solid #6c8ebf;border-radius:4px;padding:3px 8px;font-size:11px">Power Subsystem</span>
           <span style="background:#b4d7ff;border:1px solid #6c8ebf;border-radius:4px;padding:3px 8px;font-size:11px">User Mode</span>
         </div>
       </div>
       <!-- Drivers + HAL -->
       <div style="background:#e1d5e7;padding:10px 14px;border-bottom:2px solid #888">
         <div style="font-weight:bold;color:#2c3e50;text-align:center;margin-bottom:6px;font-size:14px">Device Drivers &amp; Hardware Abstraction</div>
         <div style="display:flex;flex-wrap:wrap;gap:4px;justify-content:center">
           <span style="background:#d4c4e0;border:1px solid #9673a6;border-radius:4px;padding:3px 8px;font-size:11px">Devicetree</span>
           <span style="background:#d4c4e0;border:1px solid #9673a6;border-radius:4px;padding:3px 8px;font-size:11px">GPIO, SPI, I2C, UART, ...</span>
           <span style="background:#d4c4e0;border:1px solid #9673a6;border-radius:4px;padding:3px 8px;font-size:11px">Sensor Drivers</span>
           <span style="background:#d4c4e0;border:1px solid #9673a6;border-radius:4px;padding:3px 8px;font-size:11px">Vendor HALs (as modules)</span>
           <span style="background:#d4c4e0;border:1px solid #9673a6;border-radius:4px;padding:3px 8px;font-size:11px">Architecture Support (ARM, RISC-V, x86, ...)</span>
         </div>
       </div>
       <!-- Hardware -->
       <div style="background:#f5f5f5;padding:10px 14px;text-align:center">
         <div style="font-weight:bold;color:#2c3e50;font-size:14px">Hardware</div>
         <div style="font-size:11px;color:#555">Boards, SoCs, and peripherals from hundreds of vendors</div>
       </div>
     </div>
     <!-- Legend -->
     <div style="margin-top:8px;font-size:11px;color:#666;text-align:center">
       ▲ Development tools operate across all layers &nbsp;│&nbsp; Stack layers are modular and configurable via Kconfig
     </div>
   </div>

The rest of this page describes each element in more detail. If you are new to
Zephyr, consider starting with the :ref:`getting_started` guide.


What Makes Zephyr Different
***************************

**Configurable and modular.**
Applications include only the OS components they need. Every kernel feature,
driver, and protocol stack can be enabled or disabled through :ref:`Kconfig
<kconfig>`, keeping the final binary small and deterministic.

**Hardware described, not hard-coded.**
Zephyr uses :ref:`Devicetree <dt-guide>` to describe hardware. Board and SoC
details live in declarative data files rather than in C code, making it
straightforward to retarget an application to different hardware.

**Wide architecture and board support.**
Zephyr runs on ARM (Cortex-M, Cortex-R, Cortex-A), RISC-V, x86, ARC, Xtensa,
MIPS, SPARC, and Renesas RX processors.
Hundreds of boards are supported out of the box (see :ref:`boards`).
A POSIX architecture target (:zephyr:board:`native_sim <native_sim>`) allows
running Zephyr as a native process on the development host.

**Security-oriented.**
Zephyr supports thread-level memory protection, user mode isolation, stack
overflow detection, and is developed following secure coding guidelines
(see :ref:`security_section`).

**Standards-friendly.**
A substantial :ref:`POSIX API subset <posix_support>` is available, making it
easier to port existing code. The project also supports standard protocols
such as BSD sockets, MQTT, CoAP, LwM2M, and Bluetooth Mesh.


Development Tools and Ecosystem
*******************************

Zephyr's development workflow relies on several tools that surround the OS
itself. They are shown in the top row of the diagram above.

west --- the meta-tool
======================

:ref:`west` is the command-line swiss-army knife for Zephyr development.
It manages the multi-repository workspace (Zephyr plus external
:ref:`modules <modules>`), and provides commands for common operations:

* ``west build`` --- configure and compile an application
* ``west flash`` --- program a board
* ``west debug`` --- launch a debugger session
* ``west update`` --- synchronize all workspace repositories

See :ref:`west` for the full documentation.

Zephyr SDK
==========

The :ref:`Zephyr SDK <toolchain_zephyr_sdk>` bundles pre-built GNU toolchains
for every supported architecture, plus host tools such as QEMU (for emulation)
and OpenOCD (for on-chip debugging). It runs on Linux, macOS, and Windows.

Using the Zephyr SDK is the recommended way to get started and is required for
running some of the automated tests.

Twister --- the test runner
===========================

:ref:`Twister <twister_script>` is Zephyr's test execution and continuous
integration framework. It can:

* Discover and run tests across many boards and emulation platforms in parallel.
* Filter tests by platform, tag, or hardware capability.
* Produce reports suitable for CI systems.

Twister is the primary tool used by the project's CI pipelines and is equally
useful during local development.

Build and configuration system
==============================

Under the hood, Zephyr's :ref:`build system <build_overview>` combines three
technologies:

* **CMake** --- orchestrates the compilation and linking steps
  (see :ref:`cmake-details`).
* **Kconfig** --- handles software configuration (what features to include and
  how to tune them). See :ref:`kconfig`.
* **Devicetree** --- describes the hardware so drivers and applications can
  use it at compile time. See :ref:`dt-guide`.

An application typically provides a ``CMakeLists.txt``, a ``prj.conf``
(Kconfig fragment), and optionally devicetree overlay files.
The :ref:`application` guide walks through this in detail.


Inside the OS
*************

Kernel
======

The :ref:`kernel <kernel>` is Zephyr's core. It provides:

* :ref:`Threads <threads_v2>` with cooperative, preemptive, and
  EDF :ref:`scheduling <scheduling_v2>`.
* :ref:`Synchronization primitives <kernel_api>` --- mutexes, semaphores,
  condition variables, and events.
* :ref:`Data-passing objects <kernel_api>` --- message queues, FIFOs,
  pipes, and mailboxes.
* :ref:`Memory management <memory_management_api>` --- heaps, memory slabs,
  and memory domains for isolation.
* Timers, interrupt management, and :ref:`power management <pm-guide>`.

The kernel is intentionally small; features are compiled in only when the
application's Kconfig enables them.

Device drivers
==============

Zephyr contains a large collection of :ref:`device drivers <device_model_api>`
covering buses (GPIO, SPI, I2C, UART, CAN, USB, ...), sensors, displays, and
more (see :ref:`api_peripherals`). Drivers implement a common device model,
enabling portable application code.

Vendor-specific hardware abstraction layers (HALs) are pulled in as external
:ref:`modules <modules>` through the west workspace, keeping the main
repository hardware-neutral.

OS services and protocol stacks
===============================

Above the kernel, Zephyr provides a rich set of :ref:`OS services
<os_services>`, including:

* :ref:`Networking <networking>` --- a full IP stack (IPv4/IPv6) with
  BSD sockets, DHCPv4, DNS, MQTT, CoAP, HTTP, LwM2M, and more.
* :ref:`Bluetooth <bluetooth>` --- a BLE 5.x host and optional controller,
  plus Bluetooth Mesh.
* :ref:`USB <usb>` --- device (and host) support.
* :ref:`File systems <storage_reference>` --- VFS layer with FAT, LittleFS,
  and ext2 back-ends.
* :ref:`Logging <logging_api>`, :ref:`shell <shell_api>`, and
  :ref:`tracing <tracing>` infrastructure.
* :ref:`Device management <device_mgmt>` and firmware update (DFU) via
  MCUboot.


Where to Go Next
*****************

.. list-table::
   :widths: 30 70
   :header-rows: 0

   * - :ref:`getting_started`
     - Install the tools, build your first application, and flash it to a board.
   * - :ref:`beyond-gsg`
     - Understand toolchain choices and native development options after the
       initial setup.
   * - :ref:`application`
     - Learn how a Zephyr application is structured and built.
   * - :ref:`boards`
     - Browse all supported boards and their documentation.
   * - :ref:`kernel`
     - Explore kernel services in depth.
   * - :ref:`os_services`
     - Discover networking, Bluetooth, file systems, and other OS services.
   * - :ref:`glossary`
     - Look up Zephyr-specific terminology.


.. include:: ../../README.rst
   :start-after: start_include_here
