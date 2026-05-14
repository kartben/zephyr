.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. _snippet-thread-analyzer:

Thread Analyzer Snippet (thread-analyzer)
#########################################

.. code-block:: console

   west build -S thread-analyzer [...]

Overview
********

This snippet enables the thread analyzer with periodic automatic reports over
the logger. It is a quick way to inspect stack usage and runtime distribution
without changing application code.

This snippet is intended for regular Zephyr targets and is not available on the
native/POSIX architecture.
