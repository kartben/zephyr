.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. _snippet-debug-shell:

Debug Shell Snippet (debug-shell)
#################################

.. code-block:: console

   west build -S debug-shell [...]

Overview
********

This snippet enables an interactive shell setup that is useful during bring-up
and troubleshooting. It turns on common shell quality-of-life features plus the
kernel, device, log, and statistics commands.

Use it with any console-oriented snippet or board setup that already provides a
shell backend.
