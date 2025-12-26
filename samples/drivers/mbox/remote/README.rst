.. zephyr:code-sample:: mbox_remote
   :name: MBOX Remote Application
   :relevant-api: mbox_interface

   Remote core application for the MBOX sample.

Overview
********

This is the remote core application for the :zephyr:code-sample:`mbox` sample.
This application runs on the secondary/remote processor in a multi-core system
and communicates with the main core application using the MBOX (mailbox) API.

This application is built automatically as part of the main sample when using
sysbuild. It should not be built standalone.

See the main :zephyr:code-sample:`mbox` sample documentation at
``samples/drivers/mbox/`` for complete details on building, running, and
expected output.
