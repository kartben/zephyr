# Copyright (c) 2024 Basalte bv
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sbom_spdx)
target_sources(app PRIVATE src/main.c)

enable_testing()
include(CTest)

set(
  SPDX2_TAGVALUE_FILES
  app.spdx
  zephyr.spdx
  build.spdx
  modules-deps.spdx
)

set(
  SPDX2_JSON_FILES
  app.spdx.json
  zephyr.spdx.json
  build.spdx.json
  modules-deps.spdx.json
)

set(
  SPDX3_FILES
  app.jsonld
  zephyr.jsonld
  build.jsonld
  modules-deps.jsonld
)

set(SPDX_VERSIONS_TO_TEST "2.2" "2.3" "3.0")

# Validate the given SPDX 2.x files (tag-value or JSON) and run the content checks.
# test_suffix encodes both the version and the format, matching the gen test name.
# file_ext is the document extension passed to the content verifier (spdx / spdx.json).
function(add_spdx2_validation_tests spdx_version test_suffix files file_ext)
  foreach(spdx_file ${files})
    # Smoke test: check spdx_file is a valid SPDX 2.x document as per pyspdxtools
    get_filename_component(basename ${spdx_file} NAME_WE)

    add_test(
      NAME spdx_validate_${basename}_${test_suffix}
      COMMAND pyspdxtools -i ${CMAKE_BINARY_DIR}/spdx/${spdx_file}
    )

    set_tests_properties(
      spdx_validate_${basename}_${test_suffix}
      PROPERTIES DEPENDS spdx_gen_${test_suffix}
    )
  endforeach()

  add_test(
    NAME spdx_content_validate_${test_suffix}
    COMMAND ${PYTHON_EXECUTABLE} -m pytest
      ${CMAKE_CURRENT_SOURCE_DIR}/verify_spdx_content.py
      --build-dir ${CMAKE_BINARY_DIR}
      --spdx-version ${spdx_version}
      --spdx-file-ext ${file_ext}
      --source-dir ${CMAKE_CURRENT_SOURCE_DIR}
      -v
  )

  set_tests_properties(
    spdx_content_validate_${test_suffix}
    PROPERTIES DEPENDS spdx_gen_${test_suffix}
  )
endfunction()

function(add_spdx3_validation_tests version_suffix)
  foreach(spdx_file ${SPDX3_FILES})
    # Smoke test: check spdx_file is a valid SPDX 3.x JSON-LD document
    get_filename_component(basename ${spdx_file} NAME_WE)

    add_test(
      NAME spdx_validate_${basename}_${version_suffix}
      COMMAND spdx3-validate --quiet --spdx-version auto
                             --json ${CMAKE_BINARY_DIR}/spdx/${spdx_file}
    )

    set_tests_properties(
      spdx_validate_${basename}_${version_suffix}
      PROPERTIES DEPENDS spdx_gen_${version_suffix}
    )
  endforeach()
endfunction()

# Generate SPDX documents for the given version/format and register a gen test.
function(add_spdx_gen_test test_suffix spdx_version)
  add_test(
    NAME spdx_gen_${test_suffix}
    COMMAND west spdx -d ${CMAKE_BINARY_DIR} --spdx-version ${spdx_version} ${ARGN}
  )
  set_tests_properties(
    spdx_gen_${test_suffix}
    PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
endfunction()

foreach(spdx_version ${SPDX_VERSIONS_TO_TEST})
  string(REPLACE "." "_" version_suffix ${spdx_version})

  if(spdx_version VERSION_LESS "3.0")
    # SPDX 2.x is generated in both tag-value and JSON formats.
    add_spdx_gen_test(${version_suffix}_tagvalue ${spdx_version} --spdx-format tag-value)
    add_spdx2_validation_tests(
      ${spdx_version} ${version_suffix}_tagvalue "${SPDX2_TAGVALUE_FILES}" spdx
    )

    add_spdx_gen_test(${version_suffix}_json ${spdx_version} --spdx-format json)
    add_spdx2_validation_tests(
      ${spdx_version} ${version_suffix}_json "${SPDX2_JSON_FILES}" spdx.json
    )
  else()
    add_spdx_gen_test(${version_suffix} ${spdx_version})
    add_spdx3_validation_tests(${version_suffix})
  endif()
endforeach()
