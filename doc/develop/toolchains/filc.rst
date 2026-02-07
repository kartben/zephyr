.. _filc_toolchain:

fil-c
#####

fil-c is a C compiler that can be used as a Zephyr toolchain.

#. Download and install fil-c from the official repository.

#. Set the following environment variables to use the fil-c toolchain:

   - Set :envvar:`ZEPHYR_TOOLCHAIN_VARIANT` to ``filc``.
   - Set :envvar:`FILC_TOOLCHAIN_PATH` to the fil-c installation directory.

   For example:

   .. code-block:: console

      export ZEPHYR_TOOLCHAIN_VARIANT=filc
      export FILC_TOOLCHAIN_PATH=/opt/filc

#. To use fil-c for building Zephyr applications:

   .. zephyr-app-commands::
      :tool: west
      :app: samples/hello_world
      :goals: build
      :compact:

.. note::
   fil-c is based on LLVM/Clang and uses the LLVM lld linker. It supports
   the same architectures as LLVM, including ARM, ARM64, RISC-V, and x86.
