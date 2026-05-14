.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. _snippet-log-immediate:

Immediate Logging Snippet (log-immediate)
#########################################

.. code-block:: console

   west build -S log-immediate [...]

Overview
********

This snippet switches the logger to immediate mode and enables early console
output, which is useful when debugging early boot issues, crashes, or resets
that happen before deferred logs are flushed.

If another snippet also changes the log mode, whichever snippet is applied last
wins.
