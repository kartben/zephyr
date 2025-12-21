.. _kernel:

Kernel
######

The Zephyr kernel is the core of the operating system, providing essential
services for building embedded applications. It offers a small-footprint,
high-performance, multi-threaded execution environment designed for
resource-constrained systems.

This section documents the kernel's architecture, services, and APIs that
form the foundation for all Zephyr applications.

Kernel Services
***************

The kernel services provide the fundamental building blocks for application
development, including threading, scheduling, synchronization primitives,
and inter-thread communication mechanisms.

.. toctree::
   :maxdepth: 1

   services/index.rst

Device Driver Model
*******************

The device driver model provides a consistent framework for configuring and
initializing hardware drivers, enabling portable application code across
different platforms.

.. toctree::
   :maxdepth: 1

   drivers/index.rst

Memory Management and Protection
********************************

These sections cover memory allocation strategies for resource-constrained
systems and user mode capabilities that provide memory isolation and
protection on systems with MPU or MMU hardware.

.. toctree::
   :maxdepth: 1

   memory_management/index.rst
   usermode/index.rst

Utilities
*********

Miscellaneous utility functions and macros for use in applications and
kernel internals.

.. toctree::
   :maxdepth: 1

   util/index.rst

Timing
******

Timing services provide mechanisms for measuring code execution time and
converting between different time representations.

.. toctree::
   :maxdepth: 1

   timing_functions/index.rst
   timeutil.rst

Object Cores and Debugging
**************************

Object cores provide a kernel debugging facility for identifying and
operating on registered kernel objects.

.. toctree::
   :maxdepth: 1

   object_cores/index.rst

Code and Data Placement
***********************

These features enable precise control over code and data placement in memory
through linker-defined iterable sections and relocation mechanisms.

.. toctree::
   :maxdepth: 1

   iterable_sections/index.rst
   code-relocation.rst
