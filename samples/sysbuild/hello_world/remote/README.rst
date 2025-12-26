.. zephyr:code-sample:: sysbuild_hello_world_remote
   :name: Hello World Remote Application (Sysbuild)

   Remote application for the Sysbuild Hello World sample.

Overview
********

This is the remote core/board application for the :zephyr:code-sample:`sysbuild_hello_world`
sample. This application runs on a secondary/remote processor or core in a
multi-core or multi-board system.

This application is built automatically as part of the main sample when using
sysbuild with the appropriate configuration. It should not be built standalone.

See the main :zephyr:code-sample:`sysbuild_hello_world` sample documentation at
``samples/sysbuild/hello_world/`` for complete details on building, running,
and expected output.
