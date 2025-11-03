.. zephyr:code-sample:: instrumentation
   :name: Instrumentation

   Demonstrate the instrumentation subsystem tracing and profiling features.

Overview
********

This sample provides a comprehensive demonstration of the instrumentation
subsystem's tracing and profiling features through multiple realistic scenarios.

The demo includes four distinct scenarios:

**Scenario 1: Basic Thread Synchronization (Ping-Pong)**
   Demonstrates simple context switching between threads using semaphores.
   This is useful for observing thread interactions and synchronization overhead.

**Scenario 2: Producer-Consumer Pattern**
   Showcases a classic producer-consumer pattern using message queues,
   demonstrating inter-thread communication and potential blocking behavior.

**Scenario 3: Recursive Function Profiling**
   Features recursive Fibonacci calculations to demonstrate profiling of
   functions with varying call depths and execution times.

**Scenario 4: Variable Workload Simulation**
   Executes tasks with different workloads (light, medium, heavy) to show
   how instrumentation captures different execution patterns.

Requirements
************

A Linux host and a UART console is required to run this sample.

Building and Running
********************

Build and flash the sample as follows, changing ``mps2/an385`` for your
board.

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/instrumentation
   :host-os: unix
   :board: mps2/an385
   :goals: build flash
   :compact:

Alternatively you can run this using QEMU:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/instrumentation
   :host-os: unix
   :board: mps2/an385
   :goals: run
   :gen-args: '-DQEMU_SOCKET=y'
   :compact:

Alternative Configurations
==========================

The sample includes alternative project configurations to demonstrate different
instrumentation modes:

* **prj.conf** - Default configuration with both tracing and profiling enabled
* **prj_trace_only.conf** - Trace (callgraph) mode only for minimal overhead
* **prj_profile_only.conf** - Profile (statistical) mode only for performance analysis

To build with an alternative configuration:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/instrumentation
   :host-os: unix
   :board: mps2/an385
   :goals: build
   :gen-args: '-DCONF_FILE=prj_trace_only.conf'
   :compact:

Running and Using the Sample
=============================

After the sample is flashed to the target (or QEMU is running), it must be possible to
collect and visualize traces and profiling info using the instrumentation CLI
tool, :zephyr_file:`scripts/instrumentation/zaru.py`.

.. note::
   Please note, that this subsystem uses the ``retained_mem`` driver, hence it's necessary
   to add the proper devicetree overlay for the target board. See
   :zephyr_file:`./samples/subsys/instrumentation/boards/mps2_an385.overlay` for an example.

Connect the board's UART port to the host device and
run the :zephyr_file:`scripts/instrumentation/zaru.py` script on the host.

Source the :zephyr_file:`zephyr-env.sh` file to set the ``ZEPHYR_BASE`` variable and get
:zephyr_file:`scripts/instrumentation/zaru.py` in your PATH:

.. code-block:: console

   . zephyr-env.sh

Check instrumentation status:

.. code-block:: console

   zaru.py status

Set the tracing/profiling trigger. The sample provides multiple interesting
functions to trace depending on which scenario you want to observe:

* For **Scenario 1** (thread synchronization), trace ``get_sem_and_exec_function``
  to observe context switches:

.. code-block:: console

   zaru.py trace -v -c get_sem_and_exec_function

* For **Scenario 2** (producer-consumer), trace ``process_data`` to see
  message queue operations:

.. code-block:: console

   zaru.py trace -v -c process_data

* For **Scenario 3** (recursive profiling), trace ``fibonacci`` to analyze
  recursive call patterns:

.. code-block:: console

   zaru.py trace -v -c fibonacci

* For **Scenario 4** (variable workload), trace ``worker_thread`` to observe
  different execution patterns:

.. code-block:: console

   zaru.py trace -v -c worker_thread

Reboot target so tracing/profiling at the location is effective:

.. code-block:: console

   zaru.py reboot

Wait a few seconds for the sample to execute all scenarios, then get the traces:

.. code-block:: console

   zaru.py trace -v

Get the profile:

.. code-block:: console

   zaru.py profile -v -n 10

Or alternatively, export the traces to Perfetto (it's necessary
to reboot because ``zaru.py trace`` dumped the buffer and it's now empty):

.. code-block:: console

   zaru.py reboot
   zaru.py trace -v --perfetto --output perfetto_zephyr.json

Then, go to http://perfetto.dev, Trace Viewer, and load ``perfetto_zephyr.json``.

Demo Scenario Details
**********************

Each scenario in this demo is designed to highlight specific aspects of the
instrumentation subsystem:

**Scenario 1: Thread Synchronization**
   Best for understanding:
   
   * Context switch overhead
   * Semaphore acquisition/release timing
   * Thread state transitions
   * Basic ping-pong patterns between threads

**Scenario 2: Producer-Consumer**
   Best for understanding:
   
   * Message queue operations
   * Blocking/unblocking behavior
   * Inter-thread communication overhead
   * Queue full/empty conditions

**Scenario 3: Recursive Profiling**
   Best for understanding:
   
   * Call depth analysis
   * Recursive function overhead
   * Execution time distribution
   * Stack usage patterns

**Scenario 4: Variable Workload**
   Best for understanding:
   
   * Different workload characteristics
   * CPU utilization patterns
   * Function execution time variance
   * Thread scheduling with mixed priorities

Example Profiling Workflows
****************************

**Profiling Recursive Functions:**

.. code-block:: console

   zaru.py trace -v -c fibonacci
   zaru.py reboot
   # Wait for completion
   zaru.py profile -v -n 20

This will show the top 20 functions by execution time, with ``fibonacci``
and its recursive calls dominating the profile.

**Analyzing Thread Context Switches:**

.. code-block:: console

   zaru.py trace -v -c main
   zaru.py reboot
   # Wait for completion
   zaru.py trace -v --perfetto --output context_switches.json

Load the JSON in Perfetto to visualize all thread interactions across
all four scenarios.

**Comparing Workload Performance:**

.. code-block:: console

   zaru.py trace -v -c worker_thread
   zaru.py reboot
   # Wait for completion
   zaru.py profile -v -n 10

This will show the execution time breakdown for light, medium, and heavy
work functions, demonstrating the profiling capability for performance analysis.
