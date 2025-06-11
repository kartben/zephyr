.. zephyr:code-sample:: fortune_cookie
   :name: Fortune Cookie
   :relevant-api: random

   Display a random fortune message on the console.

Overview
********

This sample selects a line from a small list of fortunes using
:zephyr:func:`sys_rand32_get` and prints it to the console. Any board with a
console can run the example.

Building and Running
********************

Build and run for the :ref:`native_sim` board:

.. zephyr-app-commands::
   :zephyr-app: samples/entertainment/fortune_cookie
   :board: native_sim
   :goals: build run
   :compact:

Sample Output
=============

.. code-block:: console

   Fortune: Today is a great day for innovation.
