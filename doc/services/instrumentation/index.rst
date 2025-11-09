.. _instrumentation:

Instrumentation
###############

The instrumentation subsystem provides compiler-managed runtime system instrumentation capabilities
for Zephyr applications. It enables developers to trace function calls, observe context switches,
and profile application performance with minimal manual instrumentation effort.

Unlike the :ref:`tracing <tracing>` subsystem, which provides RTOS-aware tracing with structured
event APIs, the instrumentation subsystem works at a lower level by leveraging compiler
instrumentation hooks. This approach makes it possible to capture virtually any function entry and
exit events without requiring manual tracing calls in the code.

.. admonition:: Tracing vs. Instrumentation
   :class: hint

   **When to use Tracing**: Choose the tracing subsystem when you need RTOS-aware event tracing with
   third-party tool integration (SystemView, Tracealyzer) and want to minimize overhead in
   production builds.

   **When to use Instrumentation**: Choose instrumentation when you need comprehensive
   function-level profiling during development, want automatic call graph reconstruction, or need to
   identify performance bottlenecks without manually adding trace points.


The instrumentation subsystem relies on compiler support for automatic function instrumentation.
When enabled, the compiler automatically inserts calls to special instrumentation handler functions
at the entry and exit of every function in your application (excluding those explicitly marked with
``__no_instrumentation__``). Currently, only GCC is supported with the ``-finstrument-functions``
compiler flag.

The subsystem initializes automatically after RAM initialization and uses trigger/stopper functions
to control when recording is active. The default trigger and stopper functions are both set to
``main()`` (configurable via Kconfig), meaning instrumentation captures the entire execution from
when ``main()`` starts until it returns.

Operational Modes
*****************

The instrumentation subsystem supports two modes that can be enabled independently or together:

Callgraph Mode (Tracing)
=========================

In callgraph mode (enabled with :kconfig:option:`CONFIG_INSTRUMENTATION_MODE_CALLGRAPH`), the
subsystem records function entry and exit events along with timestamps and context information in a
memory buffer. This enables:

- Reconstruction of the complete function call graph
- Observation of thread context switches
- Analysis of execution flow and timing relationships

The trace buffer can operate in ring buffer mode (default, overwrites old entries) or fixed buffer
mode (stops when full). Buffer size is configurable via
:kconfig:option:`CONFIG_INSTRUMENTATION_MODE_CALLGRAPH_TRACE_BUFFER_SIZE`.

Statistical Mode (Profiling)
=============================

In statistical mode (enabled with :kconfig:option:`CONFIG_INSTRUMENTATION_MODE_STATISTICAL`), the
subsystem accumulates timing statistics for each unique function executed between the trigger and
stopper points. This provides total execution time per function and helps identify performance
bottlenecks. The subsystem tracks up to
:kconfig:option:`CONFIG_INSTRUMENTATION_MODE_STATISTICAL_MAX_NUM_FUNC` unique functions.

Configuration
*************

Enable instrumentation with:

.. code-block:: kconfig

   CONFIG_INSTRUMENTATION=y
   CONFIG_INSTRUMENTATION_MODE_CALLGRAPH=y  # For tracing
   CONFIG_INSTRUMENTATION_MODE_STATISTICAL=y  # For profiling

The instrumentation subsystem uses :ref:`retained memory <retention_api>` to persist trigger/stopper
function addresses across reboots. This must be configured in the devicetree:

.. code-block:: devicetree

   / {
       sram@2003FC00 {
           compatible = "zephyr,memory-region", "mmio-sram";
           reg = <0x2003FC00 DT_SIZE_K(1)>;
           zephyr,memory-region = "RetainedMem";

           retainedmem {
               compatible = "zephyr,retained-ram";
               status = "okay";

               instrumentation_triggers: retention@0 {
                   compatible = "zephyr,retention";
                   status = "okay";
                   reg = <0x0 0x10>;
               };
           };
       };
   };

   /* Adjust main SRAM to exclude retained region */
   &sram0 {
       reg = <0x20000000 DT_SIZE_K(255)>;
   };

See the :zephyr_file:`samples/subsys/instrumentation` sample for complete configuration examples.
Additional options include buffer sizes, trigger functions, and function/file exclusion lists (see
Kconfig options starting with ``CONFIG_INSTRUMENTATION_*``).

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

Basic Workflow
==============

1. **Build and flash** the instrumentation-enabled application:

   .. code-block:: console

      west build -b <your_board> samples/subsys/instrumentation
      west flash

2. **Check instrumentation status**:

   .. code-block:: console

      zaru.py status

3. **Set trigger and stopper functions** (optional):

   .. code-block:: console

      zaru.py trace -c my_function_to_trace

4. **Reboot** to apply configuration:

   .. code-block:: console

      zaru.py reboot

5. **Collect traces**:

   .. code-block:: console

      zaru.py trace -v --perfetto --output trace.json

6. **Collect profiling data**:

   .. code-block:: console

      zaru.py profile -v -n 10

Advanced Usage
==============

The ``zaru.py`` tool supports:

- **Custom serial port**: ``--device /dev/ttyUSB0``
- **Symbol resolution**: Automatically resolves function addresses using the ELF file
- **Perfetto export**: Generates JSON files for the Perfetto trace viewer (https://perfetto.dev)
- **CTF export**: Outputs traces in Common Trace Format

To profile a specific function, set it as the trigger/stopper, reboot, wait for execution, then
collect the profile data to identify the top functions consuming CPU time within that function.

Limitations and Considerations
*******************************

- **Compiler support**: Currently requires GCC with ``-finstrument-functions`` support. Other
  compilers (LLVM/Clang, IAR) are not supported.

- **Memory requirements**: Requires retained memory configuration in devicetree. Trace buffer size
  directly impacts RAM usage (default: 12 KB). Statistical mode requires memory for function
  tracking (default: 256 functions).

- **Stack size requirements**: Instrumentation adds overhead to every function call, which increases
  stack usage. You will likely need to increase thread stack sizes to accommodate the additional
  space required by instrumentation handlers and nested function calls.

- **Execution overhead**: All function calls incur instrumentation overhead (typically 5-20%
  execution time increase). This may significantly impact real-time performance guarantees. Code
  size typically increases by 20-40%.

- **Initialization constraints**: Cannot instrument code that runs before RAM initialization. Early
  boot functions are not captured.

- **Buffer limitations**: In ring buffer mode, old traces are lost when the buffer fills. In fixed
  buffer mode, tracing stops when full.

- **Tool dependencies**: Requires ``zaru.py`` tool with Python dependencies (babeltrace2, pyserial).
  Target must be connected via UART for data extraction. Reboot required to change trigger/stopper
  functions.

To reduce overhead, use trigger/stopper functions to instrument only code regions of interest, and
exclude performance-critical functions via
:kconfig:option:`CONFIG_INSTRUMENTATION_EXCLUDE_FUNCTION_LIST`.

API Reference
*************

.. doxygengroup:: instrumentation_api
