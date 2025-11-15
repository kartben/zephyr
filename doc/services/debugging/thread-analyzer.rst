.. _thread_analyzer:

Thread analyzer
###################

The thread analyzer module enables all the Zephyr options required to track
the thread information, e.g. thread stack size usage and other runtime thread
runtime statistics.

The analysis is performed on demand when the application calls
:c:func:`thread_analyzer_run` or :c:func:`thread_analyzer_print`.

Stack painting and high watermark tracking
*******************************************

Stack painting
==============

Stack painting is a debugging technique where thread stacks are initialized
with a known pattern (0xaa) at thread creation time. This is enabled via the
:kconfig:option:`CONFIG_INIT_STACKS` option, which is automatically selected
when :kconfig:option:`CONFIG_THREAD_ANALYZER` is enabled.

The thread analyzer uses this known pattern to detect stack usage by scanning
from the bottom of the stack upward until it encounters memory that has been
modified. This allows accurate measurement of:

* Current stack usage (bytes of stack that have been written to)
* Unused stack space (bytes still containing the 0xaa pattern)
* Stack utilization percentage

High watermark tracking
========================

When :kconfig:option:`CONFIG_THREAD_ANALYZER_STACK_HIGH_WATERMARK` is enabled,
the thread analyzer tracks the maximum stack usage observed for each thread
since its creation. This feature provides:

* Peak stack usage detection across the thread's lifetime
* Worst-case stack usage patterns
* Historical view of stack utilization
* Better insight for stack size optimization

The high watermark is stored in the thread's stack_info structure and is
updated automatically each time the thread analyzer runs. This allows
developers to identify threads that may need larger stacks or threads
with stacks that can be safely reduced.

Benefits of high watermark tracking:

* **Optimization**: Identify threads with oversized stacks that can be reduced
* **Safety**: Ensure threads have adequate stack margins for worst-case scenarios
* **Debugging**: Detect intermittent stack-intensive operations that might be
  missed with single-point measurements
* **Profiling**: Understand stack growth patterns over time

For example, to build the synchronization sample with Thread Analyser enabled,
do the following:

   .. zephyr-app-commands::
      :zephyr-app: samples/synchronization/
      :board: qemu_x86
      :goals: build
      :gen-args: -DCONFIG_QEMU_ICOUNT=n -DCONFIG_THREAD_ANALYZER=y \
                   -DCONFIG_THREAD_ANALYZER_USE_PRINTK=y -DCONFIG_THREAD_ANALYZER_AUTO=y \
                   -DCONFIG_THREAD_ANALYZER_AUTO_INTERVAL=5


When you run the generated application in Qemu, you will get the additional
information from Thread Analyzer::


	thread_a: Hello World from cpu 0 on qemu_x86!
	Thread analyze:
	 thread_b            : STACK: unused 740 usage 284 / 1024 (27 %); CPU: 0 %
	 thread_analyzer     : STACK: unused 8 usage 504 / 512 (98 %); CPU: 0 %
	 thread_a            : STACK: unused 648 usage 376 / 1024 (36 %); CPU: 98 %
	 idle                : STACK: unused 204 usage 116 / 320 (36 %); CPU: 0 %
	thread_b: Hello World from cpu 0 on qemu_x86!
	thread_a: Hello World from cpu 0 on qemu_x86!
	thread_b: Hello World from cpu 0 on qemu_x86!
	thread_a: Hello World from cpu 0 on qemu_x86!
	thread_b: Hello World from cpu 0 on qemu_x86!
	thread_a: Hello World from cpu 0 on qemu_x86!
	thread_b: Hello World from cpu 0 on qemu_x86!
	thread_a: Hello World from cpu 0 on qemu_x86!
	Thread analyze:
	 thread_b            : STACK: unused 648 usage 376 / 1024 (36 %); CPU: 7 %
	 thread_analyzer     : STACK: unused 8 usage 504 / 512 (98 %); CPU: 0 %
	 thread_a            : STACK: unused 648 usage 376 / 1024 (36 %); CPU: 9 %
	 idle                : STACK: unused 204 usage 116 / 320 (36 %); CPU: 82 %
	thread_b: Hello World from cpu 0 on qemu_x86!
	thread_a: Hello World from cpu 0 on qemu_x86!
	thread_b: Hello World from cpu 0 on qemu_x86!
	thread_a: Hello World from cpu 0 on qemu_x86!
	thread_b: Hello World from cpu 0 on qemu_x86!
	thread_a: Hello World from cpu 0 on qemu_x86!
	thread_b: Hello World from cpu 0 on qemu_x86!
	thread_a: Hello World from cpu 0 on qemu_x86!
	Thread analyze:
	 thread_b            : STACK: unused 648 usage 376 / 1024 (36 %); CPU: 7 %
	 thread_analyzer     : STACK: unused 8 usage 504 / 512 (98 %); CPU: 0 %
	 thread_a            : STACK: unused 648 usage 376 / 1024 (36 %); CPU: 8 %
	 idle                : STACK: unused 204 usage 116 / 320 (36 %); CPU: 83 %
	thread_b: Hello World from cpu 0 on qemu_x86!
	thread_a: Hello World from cpu 0 on qemu_x86!
	thread_b: Hello World from cpu 0 on qemu_x86!

Example output with high watermark tracking
============================================

When :kconfig:option:`CONFIG_THREAD_ANALYZER_STACK_HIGH_WATERMARK` is enabled
(default), the thread analyzer will also display the high watermark (peak stack
usage) for each thread::

	Thread analyze:
	 thread_b            : STACK: unused 648 usage 376 / 1024 (36 %); CPU: 7 %
	                     : High watermark: 412 / 1024 (40 %)
	 thread_analyzer     : STACK: unused 8 usage 504 / 512 (98 %); CPU: 0 %
	                     : High watermark: 508 / 512 (99 %)
	 thread_a            : STACK: unused 648 usage 376 / 1024 (36 %); CPU: 9 %
	                     : High watermark: 428 / 1024 (41 %)
	 idle                : STACK: unused 204 usage 116 / 320 (36 %); CPU: 82 %
	                     : High watermark: 128 / 320 (40 %)

In this example, you can see that while thread_b's current usage is 376 bytes,
it has peaked at 412 bytes at some point since creation. This information helps
identify whether threads need more stack space or if they can be safely reduced.

Configuration
*************
Configure this module using the following options.

:kconfig:option:`CONFIG_THREAD_ANALYZER`
   Enable the module. This automatically selects :kconfig:option:`CONFIG_INIT_STACKS`
   to enable stack painting for accurate stack usage measurement.
:kconfig:option:`CONFIG_INIT_STACKS`
   Initialize stack areas with a known value (0xaa) for stack painting. This is
   automatically enabled by :kconfig:option:`CONFIG_THREAD_ANALYZER`.
:kconfig:option:`CONFIG_THREAD_ANALYZER_STACK_HIGH_WATERMARK`
   Enable tracking of stack high watermark (peak usage) for each thread. When
   enabled, the thread analyzer maintains a record of the maximum stack usage
   observed for each thread, allowing detection of worst-case stack requirements.
   Default: enabled.
:kconfig:option:`CONFIG_THREAD_ANALYZER_USE_PRINTK`
   Use printk for thread statistics.
:kconfig:option:`CONFIG_THREAD_ANALYZER_USE_LOG`
   Use the logger for thread statistics.
:kconfig:option:`CONFIG_THREAD_ANALYZER_AUTO`
   Run the thread analyzer automatically.
   You do not need to add any code to the application when using this option.
:kconfig:option:`CONFIG_THREAD_ANALYZER_AUTO_INTERVAL`
   The time for which the module sleeps between consecutive printing of thread analysis in automatic
   mode.
:kconfig:option:`CONFIG_THREAD_ANALYZER_AUTO_STACK_SIZE`
  The stack for thread analyzer automatic thread.
:kconfig:option:`CONFIG_THREAD_NAME`
  Print the name of the thread instead of its ID.
:kconfig:option:`CONFIG_THREAD_RUNTIME_STATS`
  Print thread runtime data such as utilization.
  This options is automatically selected by :kconfig:option:`CONFIG_THREAD_ANALYZER`.
:kconfig:option:`CONFIG_THREAD_ANALYZER_LONG_FRAME_PER_INTERVAL`
  Reset Longest Frame value statistics after printing.
  When using :kconfig:option:`SCHED_THREAD_USAGE_ANALYSIS` to get average and longest
  frame thread statistics, reset the Longest Frame value to zero after each time
  printing the thread statistics.  This enables observation of the longest frame
  during the most recent interval rather than longest frame since startup.

API documentation
*****************

.. doxygengroup:: thread_analyzer
