# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

# Generate the SBOM for the same build twice, pinning the two inputs that would
# otherwise differ per run, and check that the documents come out identical.
#
# Expects BUILD_DIR, SPDX_VERSION and WORK_DIR to be passed with -D.

set(ENV{SOURCE_DATE_EPOCH} "1700000000")
set(NAMESPACE "https://example.com/zephyr-sbom-reproducibility-test")

foreach(run first second)
  set(output_dir ${WORK_DIR}/${run})
  file(REMOVE_RECURSE ${output_dir})
  file(MAKE_DIRECTORY ${output_dir})

  execute_process(
    COMMAND west spdx -d ${BUILD_DIR} -s ${output_dir} -n ${NAMESPACE}
                      --spdx-version ${SPDX_VERSION}
    RESULT_VARIABLE result
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "west spdx failed on the ${run} run (exit code ${result})")
  endif()
endforeach()

file(GLOB documents RELATIVE ${WORK_DIR}/first ${WORK_DIR}/first/*)
if(NOT documents)
  message(FATAL_ERROR "no SPDX documents were generated in ${WORK_DIR}/first")
endif()

foreach(document ${documents})
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E compare_files
            ${WORK_DIR}/first/${document} ${WORK_DIR}/second/${document}
    RESULT_VARIABLE differs
  )
  if(NOT differs EQUAL 0)
    message(
      FATAL_ERROR
      "${document} differs between two runs over the same build; SBOM generation is "
      "not reproducible even with SOURCE_DATE_EPOCH and a fixed namespace prefix"
    )
  endif()
endforeach()
