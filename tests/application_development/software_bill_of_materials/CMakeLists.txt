# Copyright (c) 2024 Basalte bv
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sbom_spdx)
target_sources(app PRIVATE src/main.c)

enable_testing()
include(CTest)

set(SPDX_VERSIONS_TO_TEST "2.2" "2.3" "3.0")

foreach(spdx_version ${SPDX_VERSIONS_TO_TEST})
  string(REPLACE "." "_" version_suffix ${spdx_version})

  # Step 1: Generate SPDX documents
  add_test(
    NAME spdx_gen_${version_suffix}
    COMMAND west spdx -d ${CMAKE_BINARY_DIR} --spdx-version ${spdx_version}
  )

  # Step 2: Schema validation (version-dependent)
  if(spdx_version VERSION_LESS "3.0")
    # SPDX 2.x: validate tag-value files with pyspdxtools
    set(SPDX_2X_FILES app.spdx zephyr.spdx build.spdx modules-deps.spdx)

    foreach(spdx_file ${SPDX_2X_FILES})
      get_filename_component(basename ${spdx_file} NAME_WE)

      add_test(
        NAME spdx_validate_${basename}_${version_suffix}
        COMMAND pyspdxtools -i ${CMAKE_BINARY_DIR}/spdx/${spdx_file}
      )

      set_tests_properties(
        spdx_validate_${basename}_${version_suffix}
        PROPERTIES DEPENDS spdx_gen_${version_suffix}
      )
    endforeach()
  else()
    # SPDX 3.0: validate JSON-LD structure
    set(SPDX_3X_FILES
      app-spdx3.jsonld
      zephyr-spdx3.jsonld
      build-spdx3.jsonld
      modules-deps-spdx3.jsonld
    )

    foreach(spdx_file ${SPDX_3X_FILES})
      string(REPLACE "-spdx3.jsonld" "" basename ${spdx_file})

      add_test(
        NAME spdx_validate_${basename}_${version_suffix}
        COMMAND ${PYTHON_EXECUTABLE} -c
          "import json,sys; d=json.load(open(sys.argv[1])); assert '@graph' in d and '@context' in d, 'Missing @graph or @context'; [e.get('type') or (_ for _ in ()).throw(ValueError(f'Element missing type')) for e in d['@graph']]"
          ${CMAKE_BINARY_DIR}/spdx/${spdx_file}
      )

      set_tests_properties(
        spdx_validate_${basename}_${version_suffix}
        PROPERTIES DEPENDS spdx_gen_${version_suffix}
      )
    endforeach()
  endif()

  # Step 3: Content validation with pytest (generic, all versions)
  add_test(
    NAME spdx_content_validate_${version_suffix}
    COMMAND ${PYTHON_EXECUTABLE} -m pytest
      ${CMAKE_CURRENT_SOURCE_DIR}/verify_spdx_content.py
      --build-dir ${CMAKE_BINARY_DIR}
      --spdx-version ${spdx_version}
      --source-dir ${CMAKE_CURRENT_SOURCE_DIR}
      -v
  )

  set_tests_properties(
    spdx_content_validate_${version_suffix}
    PROPERTIES DEPENDS spdx_gen_${version_suffix}
  )

  # Step 4: SPDX 3.0-specific tests
  if(NOT spdx_version VERSION_LESS "3.0")
    add_test(
      NAME spdx3_specific_validate_${version_suffix}
      COMMAND ${PYTHON_EXECUTABLE} -m pytest
        ${CMAKE_CURRENT_SOURCE_DIR}/verify_spdx3_specific.py
        --build-dir ${CMAKE_BINARY_DIR}
        --spdx-version ${spdx_version}
        --source-dir ${CMAKE_CURRENT_SOURCE_DIR}
        -v
    )

    set_tests_properties(
      spdx3_specific_validate_${version_suffix}
      PROPERTIES DEPENDS spdx_gen_${version_suffix}
    )
  endif()
endforeach()
