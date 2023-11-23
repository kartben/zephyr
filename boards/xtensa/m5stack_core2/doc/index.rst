Zephyr High Level Requirements
##############################

.. _ZEP-1:

Architecture Layer Interface
============================

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-1
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Hardware Architecture Interface

Zephyr shall provide a framework to communicate with a set of hardware architectural services.

**USER_STORY:**

As a Zephyr OS user I want to be able to easily switch my application to a different MCU architecture (x86, ARM Cortex-M/A, RISCV etc.).

**DISCUSSION_DATE:**

2022/3/30 - ok - pf

**Children:**

- ``[ZEP-3]`` :ref:`ZEP-3`
- ``[ZEP-8]`` :ref:`ZEP-8`
- ``[ZEP-9]`` :ref:`ZEP-9`
- ``[ZEP-10]`` :ref:`ZEP-10`
- ``[ZEP-11]`` :ref:`ZEP-11`

.. _ZEP-3:

Support multiprocessor management
=================================

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-3
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Hardware Architecture Interface

Zephyr shall support symmetric multiprocessing on multiple cores.

**USER_STORY:**

As a Zephyr OS user I want to use Zephyr OS on multi core (SMP-)MCUs/MPUs.

**DISCUSSION_DATE:**

TBD: Still need to articulate the capabilities explicitly.

**REVIEW_COMMENT:**

From the Docs: No special application code needs to be written to take advantage of this feature

**Parents:**

- ``[ZEP-1]`` :ref:`ZEP-1`

.. _ZEP-2:

Support Subset of Standard C Library
====================================

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-2
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - C Library

Zephyr shall support a subset of the standard C library.

**USER_STORY:**

As a Zephyr OS user I want to have a selection of standard C library implementations e.g. a full extend and a minimal with a smaller footprint or a particular fast executing implementation.

**DISCUSSION_DATE:**

We needed to do a subset description, and need to be specific about the subset.

**REVIEW_COMMENT:**

Can we limit the type of C library implementations? Testing might be hell if we do not limit ourselfes to the ones defined? Like "miminal libc" and "newlib". Will it make sense to pull miminal Libc into the certification scope?

Clarification: Would prefer to cover the scope part first. Talking about the hooks which apply to each C Library. Open, Close, Diff. Hooks that are OS dependent. C or Library dependent. ref https://github.com/zephyrproject-rtos/zephyr/blob/main/lib/libc/newlib/libc-hooks.c

TBD: The requirement needs refinement.

**Children:**

- ``[ZEP-12]`` :ref:`ZEP-12`
- ``[ZEP-13]`` :ref:`ZEP-13`
- ``[ZEP-14]`` :ref:`ZEP-14`
- ``[ZEP-15]`` :ref:`ZEP-15`
- ``[ZEP-16]`` :ref:`ZEP-16`
- ``[ZEP-17]`` :ref:`ZEP-17`
- ``[ZEP-18]`` :ref:`ZEP-18`
- ``[ZEP-19]`` :ref:`ZEP-19`
- ``[ZEP-20]`` :ref:`ZEP-20`
- ``[ZEP-21]`` :ref:`ZEP-21`
- ``[ZEP-22]`` :ref:`ZEP-22`

.. _ZEP-35:

Data Structures Library Utilities
=================================

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-35
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Utilities Library - Data Structures

Zephyr shall provide common container data structures as library utilities   (ring buffer, linked list, red black trees, ....   see document from Anas)

**USER_STORY:**

As a Zepyhr OS developler (user) I do not want to implement common software patterns multiple time in each module again, but make use of a common library which provides it.

**DISCUSSION_DATE:**

AI: TBD: Need to be reworked;  split into several requirements;   Library utility shall provide red-black,   shall provide ...   .   Call "Utility Libraries"

**REVIEW_COMMENT:**

Possible API - It's ambiguous what is meant by common container data structures as library ulitileis.... - get pointer to code.   (Single linked list, double linked list, ....)

Clarification: There are many files. Linked list, ring buffer, ...). This is an implementation. Need to look at scope. Doesn't need to be a high level requirement

.. _ZEP-36:

Device Driver Abstracting
=========================

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-36
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Device Driver API

Zephyr shall provide a framework for managing device driver behavior (note: device drivers includes peripherals).

**USER_STORY:**

As a Zephyr OS user I want my application to be portable between different MCU architectures (ARM Cortex-M/A, Intel x86, RISCV etc.) and MCU vendors (STM, NXP, Intel, etc.) without having to change the MCU peripherals access.

**DISCUSSION_DATE:**

2022/3/30 - ok - pf

**REVIEW_COMMENT:**

TBD: Title seems to need refinement. Also, this requirement's user story seems to be identical to that of ZEP-1.

.. _ZEP-37:

Fatal error and exception handling
==================================

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-37
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Exception and Error Handling

The Zephyr kernel shall provide a framework for error and exception handling.

**USER_STORY:**

As a Zephyr OS user I want errors and exeptions to handled and react according to my applications requirements (e.g. reach/establish the applications safey state).

**DISCUSSION_DATE:**

2022/4/13 - ok -pf

.. _ZEP-38:

Common File system operation support
====================================

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-38
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - File Systems

Zephyr shall provide a framework for managing file system access.

**USER_STORY:**

As a Zephyr OS user I want a posix / c like file system access to store data.

**DISCUSSION_DATE:**

TBD: 2022/4/13 - ok - p? - depends on set of expectations

.. _ZEP-39:

Interrupt Service Routine
=========================

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-39
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Interrupts

Zephyr shall provide a framework for interrupt management.

**USER_STORY:**

As a Zephyr OS user I want interrupts to be handled sychronously in response to a hardware or software interrupt request with a minimum latency, preemtping threads and, as far as the hardware allows, lower priority interrupt service routines.

**DISCUSSION_DATE:**

2022/4/13 - ok - pf

.. _ZEP-40:

Logging
=======

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-40
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Logging

Zephyr shall provide a framework for logging events.

**USER_STORY:**

As a Zephyr OS user I want to be able to log application defined events as well as framework exceptions.

**DISCUSSION_DATE:**

2022/4/13 - ok - pf

**REVIEW_COMMENT:**

Nicole: TBD: we need to have logging in the safety scope?

.. _ZEP-41:

Memory Management framework
===========================

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-41
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Memory Management

Zephyr shall support a memory management framework.

**USER_STORY:**

As a Zephyr OS user I want memory to be allocated and protected to my application threads preventing mistakenly access to foreign memory as far as the hardware allows.

**DISCUSSION_DATE:**

2022/10/25 - ok

.. _ZEP-42:

Power Management
================

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-42
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Power Management

Zephyr shall provide an interface to control hardware power states.

**USER_STORY:**

As a Zephyr OS user I want to be able to control the power mode of the MCU and its peripherals to take advantage of the hardward features and to be able to implement low power or battery driven long life applications.

**DISCUSSION_DATE:**

2022/5/25 - ok pf-ok

.. _ZEP-43:

Mutex
=====

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-43
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Thread Communication

Zephyr shall provide an interface for managing communcation between threads.

**USER_STORY:**

As a Zephyr OS user I want to able to exchange information between threads in a thread-safe manner guaranteeing data consistence.

**DISCUSSION_DATE:**

2022/5/25 - ok  - pf-ok

.. _ZEP-44:

Multiple CPU scheduling
=======================

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-44
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Thread Mapping (should it just be scheduling)

Zephyr shall support scheduling of threads on multiple hardware CPUs.

**USER_STORY:**

As a Zephyr OS user I want Zephyr OS to run on MCUs/CPUs with one or more CPU cores.

**DISCUSSION_DATE:**

pf-ok

.. _ZEP-4:

Scheduling
==========

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-4
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Thread Scheduling

Zephyr shall provide an interface to assign a thread to a specific CPU.

**USER_STORY:**

As a Zephyr OS user, I want to be able to control which thread will run on which CPU.

**DISCUSSION_DATE:**

pf-ok

**REVIEW_COMMENT:**

TBD: The statement contains two separate statements.

.. _ZEP-5:

Managing threads
================

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-5
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Threads

Zephyr shall provide a framework for managing multiple threads of execution.

**USER_STORY:**

As a Zephyr OS user, I want to be able to manage the execute of multiple threads with different priorities.

**DISCUSSION_DATE:**

pf-ok

**REVIEW_COMMENT:**

TBD: Nothing about priorities...

.. _ZEP-6:

Timers
======

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-6
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Timers

Zephyr shall provide a framework for managing time-based events.

**USER_STORY:**

As a Zephyr OS user, I want to start, suspend, resume and stop timers which shall trigger an event on a set expiration time.

**DISCUSSION_DATE:**

pf-ok

**REVIEW_COMMENT:**

TBD: Do we need requirements on scheduling latency?

**Children:**

- ``[ZEP-27]`` :ref:`ZEP-27`
- ``[ZEP-28]`` :ref:`ZEP-28`

.. _ZEP-7:

Tracing
=======

.. list-table::
    :align: left
    :header-rows: 0

    * - **UID:**
      - ZEP-7
    * - **STATUS:**
      - Draft
    * - **TYPE:**
      - High Level
    * - **COMPONENT:**
      - Tracing

Zepyhr shall provide a framework mechanism for tracing low level system operations  (NOTE: system calls, interrupts, kernel calls, thread, synchronization, etc.).

**USER_STORY:**

As a Zephyr OS user, I want to be able to trace different OS operations.

**DISCUSSION_DATE:**

Moved

**REVIEW_COMMENT:**

TBD: What are low level system operations in this context?
