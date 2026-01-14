.. _kernel:

Kernel
######

The Zephyr kernel lies at the heart of every Zephyr application. It provides
a low footprint, high performance, multi-threaded execution environment
with a rich set of available features. The rest of the Zephyr ecosystem,
including device drivers, networking stack, and application-specific code,
uses the kernel's features to create a complete application.

This section describes the kernel's capabilities and how to use them.

Kernel Services
***************

The kernel provides essential services for thread management, scheduling,
synchronization, and inter-thread communication. These form the foundation
of any Zephyr application.

.. toctree::
   :maxdepth: 1

   services/index.rst

Device Driver Model
*******************

The device driver model provides a consistent interface for configuring and
initializing hardware drivers. It enables portable application code that works
across different hardware platforms.

.. toctree::
   :maxdepth: 1

   drivers/index.rst

User Mode
*********

User mode enables running threads at a reduced privilege level for enhanced
security and isolation. This is especially useful on devices with Memory
Protection Unit (MPU) hardware.

.. toctree::
   :maxdepth: 1

   usermode/index.rst

Memory Management
*****************

The kernel provides various memory management facilities including heap
allocation, memory slabs, memory pools, and virtual memory support for
platforms with MMU hardware.

.. toctree::
   :maxdepth: 1

   memory_management/index.rst

Data Structures
***************

Zephyr provides a library of common general purpose data structures used
within the kernel and available for application code. These include linked
lists, balanced trees, and ring buffers.

.. toctree::
   :maxdepth: 1

   data_structures/index.rst

Timing and Clocks
*****************

These pages cover timing-related functionality including system clocks,
timers, and utilities for time measurement and synchronization.

.. toctree::
   :maxdepth: 1

   timing_functions/index.rst
   timeutil.rst

Debugging and Instrumentation
*****************************

Tools and APIs for debugging, profiling, and inspecting kernel objects
at runtime.

.. toctree::
   :maxdepth: 1

   object_cores/index.rst

Utilities
*********

Additional kernel utilities and infrastructure for advanced use cases.

.. toctree::
   :maxdepth: 1

   util/index.rst
   iterable_sections/index.rst
   code-relocation.rst
