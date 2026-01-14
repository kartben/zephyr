.. _kernel:

Kernel
######

This section covers the Zephyr kernel primitives and services: threads,
scheduling, synchronization, memory management, and the device driver model.

Kernel Services
***************

Threading, scheduling, synchronization primitives, and inter-thread
communication mechanisms.

.. toctree::
   :maxdepth: 1

   services/index.rst

Device Driver Model
*******************

Framework for configuring and initializing hardware drivers.

.. toctree::
   :maxdepth: 1

   drivers/index.rst

Memory Management and Protection
********************************

Memory allocation and user mode for memory isolation on MPU/MMU systems.

.. toctree::
   :maxdepth: 1

   memory_management/index.rst
   usermode/index.rst

Utilities
*********

Utility functions and macros from ``<sys/util.h>``.

.. toctree::
   :maxdepth: 1

   util/index.rst

Timing
******

Execution time measurement and time representation conversion.

.. toctree::
   :maxdepth: 1

   timing_functions/index.rst
   timeutil.rst

Object Cores and Debugging
**************************

Debugging facility for registered kernel objects.

.. toctree::
   :maxdepth: 1

   object_cores/index.rst

Code and Data Placement
***********************

Linker-defined iterable sections and code relocation.

.. toctree::
   :maxdepth: 1

   iterable_sections/index.rst
   code-relocation.rst
