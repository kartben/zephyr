.. zephyr:code-sample:: mbox_data_remote
   :name: MBOX Data Remote Application
   :relevant-api: mbox_interface

   Remote core application for the MBOX Data sample.

Overview
********

This is the remote core application for the :zephyr:code-sample:`mbox_data` sample.
This application runs on the secondary/remote processor in a multi-core system
and communicates with the main core application using the MBOX (mailbox) API
in data transfer mode.

This application is built automatically as part of the main sample when using
sysbuild. It should not be built standalone.

See the main :zephyr:code-sample:`mbox_data` sample documentation at
``samples/drivers/mbox_data/`` for complete details on building, running, and
expected output.
