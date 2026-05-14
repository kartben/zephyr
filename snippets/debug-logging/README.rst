.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. _snippet-debug-logging:

Debug Logging Snippet (debug-logging)
#####################################

.. code-block:: console

   west build -S debug-logging [...]

Overview
********

This snippet enables richer day-to-day logging with debug-level output,
runtime filtering, ``printk()`` forwarding, a larger log buffer, and thread
name prefixes.

It works with the application's existing log backend and combines well with
console snippets such as ``serial-console`` or ``rtt-console``.
