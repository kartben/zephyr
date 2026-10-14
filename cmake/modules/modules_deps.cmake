# SPDX-License-Identifier: Apache-2.0
#
# Copyright The Zephyr Project Contributors

# Check that west projects required by this build (SoC, drivers implied
# by DTS, shields, application) are present in the current workspace.
#
# Outcome:
# - ZEPHYR_REQUIRED_MODULES is set to the resolved project names.
# - Configuration fails with a fetch hint when a required module is missing.
#
# Missing modules produce a warning by default so existing custom
# ZEPHYR_MODULES setups stay buildable while metadata is completed.
# Set ZEPHYR_REQUIRE_MODULE_DEPS=ON to turn missing modules into a
# configuration error (recommended for opt-in / CI workflows).
# The check is skipped when ZEPHYR_SKIP_MODULE_DEPS is true.

include_guard(GLOBAL)

include(python)
include(extensions)

zephyr_get(ZEPHYR_SKIP_MODULE_DEPS)
zephyr_get(ZEPHYR_REQUIRE_MODULE_DEPS)

if(ZEPHYR_SKIP_MODULE_DEPS OR NOT DEFINED BOARD)
  return()
endif()

if(NOT EXISTS ${ZEPHYR_BASE}/scripts/list_modules.py)
  return()
endif()

set(list_modules_args --board ${BOARD} --zephyr-base ${ZEPHYR_BASE} --cmake)
if(DEFINED BOARD_ROOT)
  list(TRANSFORM BOARD_ROOT PREPEND "--board-root=" OUTPUT_VARIABLE module_board_root_args)
  list(APPEND list_modules_args ${module_board_root_args})
endif()
if(DEFINED SOC_ROOT)
  list(TRANSFORM SOC_ROOT PREPEND "--soc-root=" OUTPUT_VARIABLE module_soc_root_args)
  list(APPEND list_modules_args ${module_soc_root_args})
endif()
if(DEFINED SHIELD_AS_LIST)
  foreach(shield ${SHIELD_AS_LIST})
    list(APPEND list_modules_args --shield ${shield})
  endforeach()
endif()
if(DEFINED APPLICATION_SOURCE_DIR)
  list(APPEND list_modules_args --app ${APPLICATION_SOURCE_DIR})
endif()

execute_process(
  COMMAND ${PYTHON_EXECUTABLE} ${ZEPHYR_BASE}/scripts/list_modules.py
          ${list_modules_args}
  WORKING_DIRECTORY ${ZEPHYR_BASE}
  OUTPUT_VARIABLE list_modules_output
  ERROR_VARIABLE list_modules_error
  RESULT_VARIABLE list_modules_result
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(list_modules_result)
  # Incomplete metadata or an out-of-tree board without a listing should
  # not break existing builds.
  message(VERBOSE "Skipping module dependency check: ${list_modules_error}")
  return()
endif()

if(NOT list_modules_output MATCHES "set\\(ZEPHYR_REQUIRED_MODULES")
  return()
endif()

string(REGEX REPLACE ".*set\\(ZEPHYR_REQUIRED_MODULES ([^)]*)\\).*" "\\1"
       ZEPHYR_REQUIRED_MODULES "${list_modules_output}")
set(ZEPHYR_REQUIRED_MODULES ${ZEPHYR_REQUIRED_MODULES})

if(NOT ZEPHYR_REQUIRED_MODULES)
  return()
endif()

set(missing_modules)
foreach(mod ${ZEPHYR_REQUIRED_MODULES})
  string(REGEX REPLACE "[^A-Za-z0-9]" "_" mod_upper "${mod}")
  string(TOUPPER "${mod_upper}" mod_upper)
  if(NOT ZEPHYR_${mod_upper}_MODULE_DIR)
    list(APPEND missing_modules ${mod})
  endif()
endforeach()

if(missing_modules)
  set(fetch_hint "west modules fetch")
  if(DEFINED BOARD)
    string(APPEND fetch_hint " -b ${BOARD}")
    if(BOARD_QUALIFIERS)
      string(APPEND fetch_hint "/${BOARD_QUALIFIERS}")
    endif()
  endif()
  if(DEFINED SHIELD_AS_LIST)
    foreach(shield ${SHIELD_AS_LIST})
      string(APPEND fetch_hint " --shield ${shield}")
    endforeach()
  endif()
  if(DEFINED APPLICATION_SOURCE_DIR)
    string(APPEND fetch_hint " --app ${APPLICATION_SOURCE_DIR}")
  endif()
  list(JOIN missing_modules ", " missing_modules_txt)
  set(module_deps_message
    "Required west project(s) not found: ${missing_modules_txt}\nThese come from SoC metadata, driver Kconfig (ZEPHYR_*_MODULE), bindings, and the application.\nFetch only the projects needed for this build:\n  ${fetch_hint}\nOr fetch the full default workspace:\n  west update\nTo skip this check, configure -DZEPHYR_SKIP_MODULE_DEPS=ON."
  )
  if(ZEPHYR_REQUIRE_MODULE_DEPS)
    message(FATAL_ERROR ${module_deps_message})
  else()
    message(WARNING ${module_deps_message})
  endif()
endif()
