# Copyright (c) 2024 Nordic Semiconductor
#
# SPDX-License-Identifier: Apache-2.0

# This sysbuild CMake file sets the sysbuild controlled settings as properties
# on all images.

if(SB_CONFIG_COMPILER_WARNINGS_AS_ERRORS)
  set_config_bool(${ZCMAKE_APPLICATION} CONFIG_COMPILER_WARNINGS_AS_ERRORS y)
endif()

# When an SBOM has been requested for this build (`west spdx --init` created the
# CMake file-based API query at the sysbuild top level), make sure every image
# emits the build metadata file that `west spdx` consumes.
if(EXISTS ${CMAKE_BINARY_DIR}/.cmake/api/v1/query/codemodel-v2)
  set_config_bool(${ZCMAKE_APPLICATION} CONFIG_BUILD_OUTPUT_META y)
endif()
