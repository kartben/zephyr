# SPDX-License-Identifier: Apache-2.0
#
# Copyright (c) 2026 The Zephyr Project Contributors

# After Kconfig has been imported, evaluate ZEPHYR_<MODULE>_MODULE_REQUIRED
# symbols against currently available modules and write a machine-readable
# artifact. CMake never fetches repositories.

include_guard(GLOBAL)

function(zephyr_check_module_requirements)
  set(map_file ${KCONFIG_BINARY_DIR}/module-requirement-map.json)
  set(result_file ${PROJECT_BINARY_DIR}/modules-required.json)

  # Drop any previous artifact so a later tool cannot treat a stale file
  # as the result of this configure invocation.
  if(EXISTS ${result_file})
    file(REMOVE ${result_file})
  endif()

  if(NOT EXISTS ${map_file})
    return()
  endif()

  execute_process(
    COMMAND
    ${PYTHON_EXECUTABLE} ${ZEPHYR_BASE}/scripts/zephyr_module.py
    --evaluate-requirements
    --dotconfig ${DOTCONFIG}
    --requirements-map-out ${map_file}
    --requirements-result-out ${result_file}
    WORKING_DIRECTORY ${ZEPHYR_BASE}
    ERROR_VARIABLE module_req_error_text
    RESULT_VARIABLE module_req_return
  )

  if(module_req_return)
    message(FATAL_ERROR "${module_req_error_text}")
  endif()

  if(NOT EXISTS ${result_file})
    return()
  endif()

  file(READ ${result_file} module_req_json)
  string(JSON missing_count LENGTH "${module_req_json}" missing)
  if(missing_count EQUAL 0)
    return()
  endif()

  set(missing_names)
  math(EXPR missing_last "${missing_count} - 1")
  foreach(index RANGE ${missing_last})
    string(JSON name GET "${module_req_json}" missing ${index})
    list(APPEND missing_names "  ${name}")
  endforeach()
  list(JOIN missing_names "\n" missing_text)

  message(FATAL_ERROR
    "The current Zephyr configuration requires modules which are not available:\n"
    "\n"
    "${missing_text}\n"
    "\n"
    "Without west, provide each module through ZEPHYR_MODULES or "
    "EXTRA_ZEPHYR_MODULES.\n"
    "\n"
    "If this is a west workspace, obtain the project using the URL, revision, "
    "and path from the resolved west manifest. Named `west update <project>` "
    "works for projects defined in the workspace manifest repository. West "
    "currently cannot selectively update a project that exists only via an "
    "imported manifest; do not use project-filter or group-filter as a "
    "package manager.\n"
    "\n"
    "A machine-readable requirement list was written to:\n"
    "  ${result_file}\n"
  )
endfunction()
