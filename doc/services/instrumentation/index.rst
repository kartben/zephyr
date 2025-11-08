.. _instrumentation:

Instrumentation
###############

Overview
********

The instrumentation subsystem provides compiler-managed runtime system instrumentation
capabilities for Zephyr applications. It enables developers to trace function calls,
observe context switches, and profile application performance with minimal manual
instrumentation effort.

Unlike the :ref:`tracing <tracing>` subsystem, which provides RTOS-aware tracing with
structured event APIs, the instrumentation subsystem works at a lower level by leveraging
compiler instrumentation hooks. This approach automatically captures all function entry
and exit events without requiring manual tracing calls in the code.

The subsystem supports two primary operational modes:

- **Callgraph mode (Tracing)**: Captures function call sequences and context switches,
  enabling reconstruction of the program's execution flow
- **Statistical mode (Profiling)**: Collects timing statistics to identify performance
  bottlenecks and measure function execution times

How It Works
************

The instrumentation subsystem relies on compiler support for automatic function
instrumentation. When enabled, the compiler automatically inserts calls to special
instrumentation handler functions at the entry and exit of every function in your
application (excluding those explicitly marked with ``__no_instrumentation__``).

Compiler Integration
====================

Currently, the subsystem supports GCC's instrumentation hooks using the
``-finstrument-functions`` compiler flag. The compiler automatically generates calls to:

- ``__cyg_profile_func_enter(void *callee, void *caller)`` at function entry
- ``__cyg_profile_func_exit(void *callee, void *caller)`` at function exit

These handlers are implemented in the Zephyr instrumentation subsystem and process
the events according to the configured mode (tracing and/or profiling).

.. note::
   Currently, only GCC is supported. Other compilers may be added in the future if
   they provide equivalent instrumentation capabilities.

Initialization and Control
===========================

The instrumentation subsystem initializes automatically when certain conditions are met:

1. **Early boot protection**: Instrumentation waits until RAM is properly initialized
   before attempting to operate. This is verified using a "magic number" check to ensure
   variables can be safely accessed.

2. **Automatic initialization**: When the first instrumented function is called after
   RAM initialization, the subsystem automatically initializes itself.

3. **Trigger-based activation**: Instrumentation recording only begins when a designated
   "trigger" function is called and stops when a "stopper" function exits. This allows
   focusing on specific code regions of interest.

The default trigger and stopper functions are both set to ``main()`` (configurable via
Kconfig), meaning instrumentation captures the entire execution from when ``main()``
starts until it returns.

Operational Modes
*****************

Callgraph Mode (Tracing)
=========================

In callgraph mode (enabled with :kconfig:option:`CONFIG_INSTRUMENTATION_MODE_CALLGRAPH`),
the subsystem records function entry and exit events along with timestamps and context
information in a memory buffer. This enables:

- Reconstruction of the complete function call graph
- Observation of thread context switches
- Analysis of execution flow and timing relationships

The trace buffer can operate in two modes:

- **Ring buffer mode** (default): When the buffer fills, old entries are overwritten
  with new ones, ensuring the most recent events are always available
- **Fixed buffer mode**: Recording stops when the buffer is full, preserving the
  initial execution trace

Buffer size is configurable via
:kconfig:option:`CONFIG_INSTRUMENTATION_MODE_CALLGRAPH_TRACE_BUFFER_SIZE`. Larger
buffers capture more events but consume more RAM.

Statistical Mode (Profiling)
=============================

In statistical mode (enabled with
:kconfig:option:`CONFIG_INSTRUMENTATION_MODE_STATISTICAL`), the subsystem accumulates
timing statistics for each unique function executed between the trigger and stopper
points. This provides:

- Total execution time per function
- Performance bottleneck identification
- Understanding of where CPU cycles are spent

The subsystem tracks up to
:kconfig:option:`CONFIG_INSTRUMENTATION_MODE_STATISTICAL_MAX_NUM_FUNC` unique functions.
Functions discovered beyond this limit are not profiled.

Both modes can be enabled simultaneously to collect comprehensive execution data.

Retained Memory Usage
**********************

The instrumentation subsystem uses :ref:`retained memory <retention_api>` to persist
configuration data across device reboots. Specifically, it stores:

- Trigger function address: The function whose entry activates instrumentation
- Stopper function address: The function whose exit deactivates instrumentation

This design enables a powerful workflow:

1. Boot the device with default trigger/stopper functions
2. Use the ``zaru.py`` tool to dynamically set different trigger/stopper functions
3. Reboot the device to apply the new configuration
4. Instrumentation automatically uses the persisted addresses

The retained memory requirement is small (typically 8-16 bytes) but must be configured
in the device tree. See the :zephyr_file:`samples/subsys/instrumentation` sample for
devicetree overlay examples.

.. note::
   Boards must have a retained memory driver configured. The most common approach is
   using the ``zephyr,retained-ram`` driver, which reserves a small RAM region that
   is not initialized during boot.

Performance Considerations
**************************

Code Size Impact
================

Enabling instrumentation significantly increases code size due to:

- Additional handler code in the subsystem itself
- Compiler-inserted call instructions at every function entry/exit
- Metadata storage for buffer management and statistics

Typical overhead is 20-40% code size increase, depending on application complexity
and the number of functions.

Execution Overhead
==================

Instrumentation adds runtime overhead to every function call:

- **Handler execution**: Each function entry and exit executes instrumentation handler
  code, including timestamp collection and buffer management
- **Cache effects**: Additional code can impact instruction cache performance
- **Memory bandwidth**: Writing trace data to RAM consumes memory bandwidth

The overhead varies but typically ranges from 5-20% execution time increase for
compute-intensive applications. Applications with many small functions experience
higher relative overhead.

Mitigation Strategies
=====================

Several techniques can reduce instrumentation impact:

1. **Selective instrumentation**: Use trigger and stopper functions to instrument only
   code regions of interest

2. **Function exclusion**: Exclude performance-critical functions using
   :kconfig:option:`CONFIG_INSTRUMENTATION_EXCLUDE_FUNCTION_LIST` or file exclusion
   via :kconfig:option:`CONFIG_INSTRUMENTATION_EXCLUDE_FILE_LIST`

3. **Buffer tuning**: Adjust buffer sizes to balance memory usage and trace depth

4. **Disable unused modes**: If only profiling is needed, the memory and CPU cost of
   tracing can be avoided by disabling
   :kconfig:option:`CONFIG_INSTRUMENTATION_MODE_CALLGRAPH`

Differences from Tracing Subsystem
***********************************

Zephyr provides both a :ref:`tracing subsystem <tracing>` and the instrumentation
subsystem. Understanding their differences helps choose the right tool:

.. list-table:: Tracing vs Instrumentation Comparison
   :header-rows: 1
   :widths: 30 35 35

   * - Aspect
     - Tracing Subsystem
     - Instrumentation Subsystem
   * - **Approach**
     - Explicit API calls (``sys_trace_*``)
     - Automatic compiler instrumentation
   * - **RTOS Awareness**
     - Fully RTOS-aware with kernel hooks
     - Limited (observes thread switches via hooks)
   * - **Granularity**
     - User-controlled trace points
     - All functions (unless excluded)
   * - **Overhead**
     - Low (only traced operations)
     - Higher (all function calls)
   * - **Setup Effort**
     - Manual trace point placement
     - Minimal (automatic)
   * - **Integration**
     - Works with SystemView, Tracealyzer
     - Standalone with zaru.py tool
   * - **Format Support**
     - CTF, SystemView, custom formats
     - Custom binary format + CTF export
   * - **Use Case**
     - Production-ready tracing
     - Development profiling and debugging

**When to use Tracing**: Choose the tracing subsystem when you need RTOS-aware event
tracing with third-party tool integration (SystemView, Tracealyzer) and want to
minimize overhead in production builds.

**When to use Instrumentation**: Choose instrumentation when you need comprehensive
function-level profiling during development, want automatic call graph reconstruction,
or need to identify performance bottlenecks without manually adding trace points.

Configuration
*************

Basic Configuration
===================

Enable instrumentation with:

.. code-block:: kconfig

   CONFIG_INSTRUMENTATION=y

This automatically enables required dependencies including retained memory support
and UART interrupt-driven mode for data transfer.

Mode Selection
==============

Choose operational modes:

.. code-block:: kconfig

   # Enable tracing (callgraph) mode
   CONFIG_INSTRUMENTATION_MODE_CALLGRAPH=y
   CONFIG_INSTRUMENTATION_MODE_CALLGRAPH_TRACE_BUFFER_SIZE=12000
   CONFIG_INSTRUMENTATION_MODE_CALLGRAPH_BUFFER_OVERWRITE=y

   # Enable profiling (statistical) mode
   CONFIG_INSTRUMENTATION_MODE_STATISTICAL=y
   CONFIG_INSTRUMENTATION_MODE_STATISTICAL_MAX_NUM_FUNC=256

Trigger Configuration
=====================

Set the trigger and stopper functions:

.. code-block:: kconfig

   CONFIG_INSTRUMENTATION_TRIGGER_FUNCTION="main"
   CONFIG_INSTRUMENTATION_STOPPER_FUNCTION="main"

These can be changed at runtime using the ``zaru.py`` tool (see Usage section below).

Function and File Exclusion
============================

Exclude specific functions or files from instrumentation:

.. code-block:: kconfig

   # Exclude functions by name (substring matching)
   CONFIG_INSTRUMENTATION_EXCLUDE_FUNCTION_LIST="memcpy,memset,k_busy_wait"

   # Exclude files by path (substring matching)
   CONFIG_INSTRUMENTATION_EXCLUDE_FILE_LIST="lib/,drivers/serial"

Devicetree Configuration
=========================

Configure retained memory for trigger/stopper persistence. Example for a typical board:

.. code-block:: devicetree

   / {
       sram@2003FC00 {
           compatible = "zephyr,memory-region", "mmio-sram";
           reg = <0x2003FC00 DT_SIZE_K(1)>;
           zephyr,memory-region = "RetainedMem";
           status = "okay";

           retainedmem {
               compatible = "zephyr,retained-ram";
               status = "okay";
               #address-cells = <1>;
               #size-cells = <1>;

               instrumentation_triggers: retention@0 {
                   compatible = "zephyr,retention";
                   status = "okay";
                   reg = <0x0 0x10>;
                   label = "instrumentation_triggers";
               };
           };
       };
   };

   /* Adjust main SRAM to exclude retained region */
   &sram0 {
       reg = <0x20000000 DT_SIZE_K(255)>;
   };

Usage
*****

The ``zaru.py`` command-line tool (located in :zephyr_file:`scripts/instrumentation/zaru.py`)
provides the interface for controlling instrumentation and extracting data from the target.

Prerequisites
=============

Install required Python packages:

.. code-block:: console

   # On Ubuntu/Debian
   sudo apt-get install python3-bt2

   # Using pip
   pip install pyserial colorama

The tool requires the babeltrace2 Python bindings for CTF trace processing.

Basic Workflow
==============

1. **Build and flash** the instrumentation-enabled application:

.. code-block:: console

   west build -b <your_board> samples/subsys/instrumentation
   west flash

2. **Check instrumentation status**:

.. code-block:: console

   zaru.py status

This displays whether instrumentation is enabled and shows current trigger/stopper functions.

3. **Set trigger and stopper functions** (optional):

.. code-block:: console

   # Set trigger to a specific function
   zaru.py trace -c my_function_to_trace

   # The tool will prompt for the stopper or use the same function

4. **Reboot** to apply configuration:

.. code-block:: console

   zaru.py reboot

5. **Collect traces**:

.. code-block:: console

   # Get raw trace data
   zaru.py trace -v

   # Export to Perfetto format for visualization
   zaru.py trace -v --perfetto --output trace.json

6. **Collect profiling data**:

.. code-block:: console

   # Show top 10 functions by execution time
   zaru.py profile -v -n 10

Advanced Usage
==============

The ``zaru.py`` tool supports several advanced options:

- **Custom serial port**: Use ``--device /dev/ttyUSB0`` to specify a different port
- **Symbol resolution**: Automatically resolves function addresses to names using the
  ELF file
- **Perfetto export**: Generates JSON files compatible with the Perfetto trace viewer
  (https://perfetto.dev)
- **CTF export**: Outputs traces in Common Trace Format for analysis with babeltrace

Example: Profiling a Specific Function
=======================================

To profile only a specific section of code:

.. code-block:: console

   # Build and flash your application
   west build -b mps2/an385 -p
   west flash

   # Set the function of interest as trigger/stopper
   zaru.py trace -c my_performance_critical_function

   # Reboot to apply
   zaru.py reboot

   # Wait for execution to complete
   sleep 5

   # Collect profile data
   zaru.py profile -v -n 20

This workflow identifies the top 20 functions consuming CPU time within
``my_performance_critical_function``.

Visualization with Perfetto
============================

For visual analysis of traces:

.. code-block:: console

   # Collect trace and export to Perfetto format
   zaru.py reboot
   zaru.py trace -v --perfetto --output my_trace.json

   # Open https://perfetto.dev in a web browser
   # Click "Open trace file" and load my_trace.json

Perfetto provides a timeline view showing function calls, thread activity, and timing
relationships.

Limitations and Caveats
***********************

Compiler Support
================

- Currently requires GCC compiler with ``-finstrument-functions`` support
- Not available for other compilers (LLVM/Clang, IAR, etc.)

Memory Requirements
===================

- Requires retained memory configuration in devicetree
- Trace buffer size directly impacts RAM usage (default: 12 KB)
- Statistical mode requires memory for function tracking (default: 256 functions)

Execution Overhead
==================

- All function calls incur instrumentation overhead
- Cannot be disabled for individual functions at runtime (only at compile time)
- May significantly impact real-time performance guarantees

Initialization Constraints
==========================

- Cannot instrument code that runs before RAM initialization
- Early boot functions are not captured
- The subsystem automatically handles initialization safety

Buffer Limitations
==================

- In ring buffer mode, old traces are lost when the buffer fills
- In fixed buffer mode, tracing stops when full
- No automatic buffer flushing to external storage

Tool Dependencies
=================

- Requires ``zaru.py`` tool and Python dependencies (babeltrace2, pyserial)
- Target must be connected via UART for data extraction
- Reboot required to change trigger/stopper functions

Sample Application
******************

Zephyr includes a sample application demonstrating instrumentation usage:
:zephyr:code-sample:`instrumentation`

The sample shows:

- Basic setup with devicetree overlay for retained memory
- Configuration in ``prj.conf``
- Simple application with thread context switches
- How to use ``zaru.py`` for data collection

See :zephyr_file:`samples/subsys/instrumentation/README.rst` for detailed instructions.

API Reference
*************

The instrumentation subsystem provides a runtime API for advanced use cases:

.. doxygengroup:: instrumentation_api
