.. _introducing_zephyr:

Introduction
############

Zephyr is an open source, scalable real-time operating system (RTOS) designed
for resource-constrained devices as well as more capable embedded systems. It
combines a small, configurable kernel with reusable drivers, middleware, and
services that you select at build time using :ref:`Kconfig <kconfig>` and
:ref:`devicetree <dt-guide>`.

If you're new to the project, start with the :ref:`getting_started` guide and
then continue with :ref:`developing_with_zephyr`. For deeper dives, see the
:ref:`kernel`, :ref:`os_services`, :ref:`connectivity`, and
:ref:`build_overview` sections.

Architecture at a glance
************************

The diagram below shows how applications sit on top of the Zephyr OS, which
exposes kernel services and subsystem APIs over hardware-specific drivers and
board support. Tooling such as west, the Zephyr SDK, and Twister wraps the
workflow from build to test.

.. raw:: html

   <div role="img" aria-label="Zephyr architecture: applications build on the Zephyr OS, which runs on hardware support, with west, Zephyr SDK, and Twister supporting build and test." style="display:flex; flex-wrap:wrap; gap:12px; align-items:center; justify-content:center; margin-top:12px;">
     <div style="border:1px solid #6c757d; border-radius:6px; padding:12px; min-width:180px; text-align:center;">
       <strong>Applications</strong><br>
       Samples, modules, user code
     </div>
     <div style="font-size:22px;">&#8594;</div>
     <div style="border:1px solid #6c757d; border-radius:6px; padding:12px; min-width:210px; text-align:center;">
       <strong>Zephyr OS</strong><br>
       Kernel, subsystems, services
     </div>
     <div style="font-size:22px;">&#8594;</div>
     <div style="border:1px solid #6c757d; border-radius:6px; padding:12px; min-width:210px; text-align:center;">
       <strong>Hardware Support</strong><br>
       Drivers, boards, SoCs, devicetree
     </div>
   </div>
   <div style="display:flex; flex-wrap:wrap; gap:12px; align-items:stretch; justify-content:center; margin-top:12px;">
     <div style="border:1px dashed #6c757d; border-radius:6px; padding:10px; min-width:240px; text-align:center;">
       <strong>Build &amp; Tooling</strong><br>
       west • CMake • Kconfig • Zephyr SDK
     </div>
     <div style="border:1px dashed #6c757d; border-radius:6px; padding:10px; min-width:210px; text-align:center;">
       <strong>Test &amp; CI</strong><br>
       Twister • ztest • emulators
     </div>
   </div>

Core building blocks
********************

Zephyr is organized into modular :term:`subsystem` components. Each subsystem
implements a focused capability (for example, networking or storage) and can be
configured, replaced, or omitted to match the needs of your application.

Key building blocks and where to learn more:

- **Kernel services** such as threads, synchronization, and memory management:
  see :ref:`kernel`.
- **OS services** such as logging, power management, storage, and shell support:
  see :ref:`os_services`.
- **Connectivity stacks** including Bluetooth, CAN, and IP networking:
  see :ref:`connectivity`.
- **Device model and hardware description** covering drivers, board support,
  and :ref:`devicetree <dt-guide>`:
  see :ref:`device_model_api`, :ref:`hardware_support`, and :ref:`boards`.
- **Build and configuration** with CMake, Kconfig, and sysbuild:
  see :ref:`build_overview`, :ref:`cmake-details`, and :ref:`kconfig`.
- **Security and safety guidance** for hardened and certified deployments:
  see :ref:`security` and :ref:`safety`.

Tooling and workflow
********************

The Zephyr ecosystem includes companion tools that streamline development:

- :ref:`west` is the meta-tool that manages multi-repository workspaces,
  builds, flashing, and debugging.
- The :ref:`Zephyr SDK <toolchain_zephyr_sdk>` bundles cross-compilers and
  host tools, and is the recommended default toolchain
  (see :ref:`toolchains`).
- :ref:`twister_script` and the broader :ref:`testing` guide cover automated
  build-and-test workflows; unit tests use the :ref:`test-framework`.

Supported hardware
******************

Zephyr runs on a broad set of 32-bit and 64-bit architectures. For the
architecture-level view, see :ref:`arch`; for the complete list of supported
boards and SoCs, see :ref:`boards`.

Design goals
************

Zephyr is designed to be:

- **Scalable** from tiny sensors to connected gateways, with features compiled
  in only when needed.
- **Portable** across architectures and boards without changing application
  logic.
- **Configurable** through Kconfig and devicetree to keep images small and
  deterministic.
- **Secure and safe by design**, with support for memory protection and guidance
  in :ref:`security` and :ref:`safety`.

Licensing
*********

Zephyr is permissively licensed under the `Apache 2.0 license`_ (see the
``LICENSE`` file in the project's `GitHub repo`_). Some imported components use
other licenses; details are in :ref:`Zephyr_Licensing`.

.. _Apache 2.0 license:
   https://github.com/zephyrproject-rtos/zephyr/blob/main/LICENSE

.. _GitHub repo: https://github.com/zephyrproject-rtos/zephyr


.. include:: ../../README.rst
   :start-after: start_include_here


Fundamental Terms and Concepts
******************************

See :ref:`glossary`
