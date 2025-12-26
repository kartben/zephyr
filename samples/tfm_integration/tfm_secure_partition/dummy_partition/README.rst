.. zephyr:code-sample:: tfm_secure_partition_dummy
   :name: TF-M Dummy Secure Partition
   :relevant-api: tfm_api

   Implementation of a dummy secure partition for TF-M.

Overview
********

This is the dummy partition implementation for the
:zephyr:code-sample:`tfm_secure_partition` sample. It contains the actual
secure partition code that runs in the TF-M secure environment.

The dummy partition provides a secure service that can index and retrieve
hashes of dummy secrets stored within the partition. This demonstrates how
to create custom secure partitions with TF-M.

The partition includes:

- Partition source code (``dummy_partition.c``)
- Manifest files (``tfm_dummy_partition.yaml``, ``tfm_manifest_list.yaml.in``)
- Build configuration (``CMakeLists.txt``)

This code is built as part of the TF-M secure firmware and is not built
standalone. See the parent directory's README at
``samples/tfm_integration/tfm_secure_partition/`` for complete build
instructions and usage information.

Building
********

This partition is built automatically by the TF-M build system when building
the main :zephyr:code-sample:`tfm_secure_partition` sample. Do not attempt to
build this directory standalone.
