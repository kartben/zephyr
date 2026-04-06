.. zephyr:code-sample:: scope-cleanup
   :name: Scope-based cleanup helpers
   :relevant-api: cleanup_interface

   Demonstrate automatic resource cleanup using scope_guard, scope_defer, and scope_var.

Overview
********

This sample demonstrates the :ref:`scope-based cleanup helpers <cleanup_api>`,
which provide :abbr:`RAII (Resource Acquisition Is Initialization)`/defer-style
automatic resource management in C using the compiler's ``__cleanup__`` attribute.

Four patterns are shown:

1. **scope_guard** — acquire a mutex on scope entry, release it automatically
   on scope exit (including early returns and error paths).
2. **scope_defer** — register :c:func:`k_free` to run automatically when the
   local pointer variable goes out of scope.
3. **scope_var** — define a custom scoped type with init and exit functions,
   used here to manage a heap-allocated buffer with RAII semantics.
4. **LIFO order** — nested guards are released in reverse order of acquisition,
   matching the natural nesting of locked resources.

Requirements
************

Any board supported by Zephyr.  The sample uses only the kernel heap and
mutex API, with no hardware-specific drivers.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/kernel/cleanup
   :host-os: unix
   :board: native_sim
   :goals: run
   :compact:

Sample Output
*************

.. code-block:: console

   [00:00:00.000,000] <inf> cleanup_sample: Scope-based cleanup helpers sample
   [00:00:00.000,000] <inf> cleanup_sample: --- 1. Mutex guard ---
   [00:00:00.000,000] <inf> cleanup_sample: [guard] counter = 10 (lock_count=1)
   [00:00:00.000,000] <inf> cleanup_sample: [guard] counter = 15 (lock_count=1)
   [00:00:00.000,000] <inf> cleanup_sample:     final counter: 15
   [00:00:00.000,000] <inf> cleanup_sample: --- 2. Deferred k_free ---
   [00:00:00.000,000] <inf> cleanup_sample: [defer] buffer allocated and filled (will be freed on scope exit)
   [00:00:00.000,000] <inf> cleanup_sample: [defer] buffer work done
   [00:00:00.000,000] <inf> cleanup_sample: --- 3. Custom scoped variable ---
   [00:00:00.000,000] <inf> cleanup_sample: [scope_var] allocated 128-byte buffer at 0x...
   [00:00:00.000,000] <inf> cleanup_sample: [scope_var] wrote sentinel byte 42
   [00:00:00.000,000] <inf> cleanup_sample: [scope_var] freeing 128-byte buffer
   [00:00:00.000,000] <inf> cleanup_sample: --- 4. LIFO cleanup order ---
   [00:00:00.000,000] <inf> cleanup_sample: [lifo] acquiring outer lock
   [00:00:00.000,000] <inf> cleanup_sample: [lifo] acquiring inner lock
   [00:00:00.000,000] <inf> cleanup_sample: [lifo] both locks held; leaving scope
   [00:00:00.000,000] <inf> cleanup_sample:     both locks released in reverse order
   [00:00:00.000,000] <inf> cleanup_sample: Sample complete

Configuration Options
*********************

:kconfig:option:`CONFIG_SCOPE_CLEANUP_HELPERS`
   Enable the scope-based cleanup helpers.  Requires a toolchain that
   supports the ``__cleanup__`` attribute (GCC and Clang).
