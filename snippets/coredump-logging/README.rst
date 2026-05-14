.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. _snippet-coredump-logging:

Coredump Logging Snippet (coredump-logging)
###########################################

.. code-block:: console

   west build -S coredump-logging [...]

Overview
********

This snippet enables minimal coredumps through the logging subsystem, making it
easy to capture fatal-error state from the normal console without adding a
dedicated storage backend.

It is a good production-readiness baseline when paired with a log backend that
is always accessible in the field.
