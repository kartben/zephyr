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

User Mode and Memory Protection
*******************************

User mode enables running threads with reduced privileges, providing memory
isolation and protection on systems with MPU or MMU hardware.

.. toctree::
   :maxdepth: 1

   usermode/index.rst

Memory Management
*****************

Memory management services provide dynamic memory allocation strategies
suitable for resource-constrained embedded systems.

.. toctree::
   :maxdepth: 1

   memory_management/index.rst

Data Structures
***************

The kernel provides a library of common data structures including lists,
trees, and ring buffers for use in applications and kernel internals.

.. toctree::
   :maxdepth: 1

   data_structures/index.rst

Timing and Time Utilities
*************************

Timing services provide mechanisms for measuring code execution time and
converting between different time representations.

.. toctree::
   :maxdepth: 1

   timing_functions/index.rst
   timeutil.rst

Debugging and Instrumentation
*****************************

Object cores provide a kernel debugging facility for identifying and
operating on registered kernel objects.

.. toctree::
   :maxdepth: 1

   object_cores/index.rst

OS Developer Utilities
**********************

These utilities support advanced OS development tasks such as code relocation
and working with linker-defined iterable sections.

.. toctree::
   :maxdepth: 1

   util/index.rst
   iterable_sections/index.rst
   code-relocation.rst
