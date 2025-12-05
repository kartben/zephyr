# SPDX-License-Identifier: Apache-2.0
#
# Copyright (c) 2021, Nordic Semiconductor ASA

#[=======================================================================[.rst:
snippets
--------

Snippet discovery and validation module.

This module searches for snippets in Zephyr and any modules, and validates
the ``SNIPPET`` input variable. Snippets are reusable configuration fragments
that can be applied to applications.

If ``SNIPPET`` contains an unknown snippet name, an error is raised and valid
snippets are listed.

Outcome
^^^^^^^

The following variables are set when this module completes:

.. cmake:variable:: SNIPPET_AS_LIST

   CMake list of snippet names from the ``SNIPPET`` variable.

.. cmake:variable:: SNIPPET_ROOT

   CMake list of snippet root directories (deduplicated, with ``ZEPHYR_BASE``
   appended).

The following variables may be updated:

- ``DTC_OVERLAY_FILE``
- ``OVERLAY_CONFIG``

Targets
^^^^^^^

``snippets``
   When invoked, prints a list of all valid snippets.

Optional Variables
^^^^^^^^^^^^^^^^^^

.. cmake:variable:: SNIPPET_ROOT

   Input list of snippet root directories. Should not include ``ZEPHYR_BASE``
   as it is added automatically.

#]=======================================================================]

include_guard(GLOBAL)

include(extensions)

# Warn the user if SNIPPET changes later. Such changes are ignored.
zephyr_check_cache(SNIPPET WATCH)

# Putting the body into a function prevents us from polluting the
# parent scope. We'll set our outcome variables in the parent scope of
# the function to ensure the outcome of the module.
function(zephyr_process_snippets)
  set(snippets_py ${ZEPHYR_BASE}/scripts/snippets.py)
  set(snippets_generated ${CMAKE_BINARY_DIR}/zephyr/snippets_generated.cmake)
  set_ifndef(SNIPPET_NAMESPACE SNIPPET)
  set_ifndef(SNIPPET_PYTHON_EXTRA_ARGS "")
  set_ifndef(SNIPPET_APP_DIR "${APPLICATION_SOURCE_DIR}")

  # Set SNIPPET_AS_LIST, removing snippets_generated.cmake if we are
  # running cmake again and snippets are no longer requested.
  if(NOT DEFINED SNIPPET)
    set(SNIPPET_AS_LIST "" PARENT_SCOPE)
    file(REMOVE ${snippets_generated})
  else()
    string(REPLACE " " ";" SNIPPET_AS_LIST "${${SNIPPET_NAMESPACE}}")
    set(SNIPPET_AS_LIST "${SNIPPET_AS_LIST}" PARENT_SCOPE)
  endif()

  # Set SNIPPET_ROOT.
  zephyr_get(SNIPPET_ROOT MERGE SYSBUILD GLOBAL)
  list(APPEND SNIPPET_ROOT ${SNIPPET_APP_DIR})
  list(APPEND SNIPPET_ROOT ${ZEPHYR_BASE})
  unset(real_snippet_root)
  foreach(snippet_dir ${SNIPPET_ROOT})
    # The user might have put a symbolic link in here, for example.
    file(REAL_PATH ${snippet_dir} real_snippet_dir)
    list(APPEND real_snippet_root ${real_snippet_dir})
  endforeach()
  set(SNIPPET_ROOT ${real_snippet_root})
  list(REMOVE_DUPLICATES SNIPPET_ROOT)
  set(SNIPPET_ROOT "${SNIPPET_ROOT}" PARENT_SCOPE)

  # Generate and include snippets_generated.cmake.
  # The Python script is responsible for checking for unknown
  # snippets.
  set(snippet_root_args)
  foreach(root IN LISTS SNIPPET_ROOT)
    list(APPEND snippet_root_args --snippet-root "${root}")
  endforeach()
  set(requested_snippet_args)
  foreach(snippet_name ${SNIPPET_AS_LIST})
    list(APPEND requested_snippet_args --snippet "${snippet_name}")
  endforeach()
  execute_process(COMMAND ${PYTHON_EXECUTABLE}
    ${snippets_py}
    ${snippet_root_args}
    ${requested_snippet_args}
    --cmake-out ${snippets_generated}
    ${SNIPPET_PYTHON_EXTRA_ARGS}
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
    RESULT_VARIABLE ret)
  if(${ret})
    list(JOIN SNIPPET_ROOT "\n    " snippet_root_paths)
    set(snippet_root_paths "\n    ${snippet_root_paths}")

    if(SYSBUILD)
      zephyr_get(SYSBUILD_NAME SYSBUILD GLOBAL)
      set(extra_output "Snippet roots for image ${SYSBUILD_NAME}:${snippet_root_paths}\n"
          "Note that snippets will be applied to all images unless prefixed with the image name "
          "e.g. `-D${SYSBUILD_NAME}_SNIPPET=...`, local snippet roots for one image are not set "
          "for other images in a build")
    else()
      set(extra_output "Snippet roots for application:${snippet_root_paths}")
    endif()

    message(FATAL_ERROR "${output}" ${extra_output})
  endif()
  include(${snippets_generated})

  # Create the 'snippets' target. Each snippet is printed in a
  # separate command because build system files are not fond of
  # newlines.
  list(TRANSFORM SNIPPET_NAMES PREPEND "COMMAND;${CMAKE_COMMAND};-E;echo;"
    OUTPUT_VARIABLE snippets_target_cmd)
  add_custom_target(snippets ${snippets_target_cmd} USES_TERMINAL)

  # If snippets were requested, print messages for each one.
  if(SNIPPET_AS_LIST)
    # Print the requested snippets.
    set(snippet_names "Snippet(s):")
    foreach(snippet IN LISTS SNIPPET_AS_LIST)
      string(APPEND snippet_names " ${snippet}")
    endforeach()
    message(STATUS "${snippet_names}")
  endif()

  # Re-run cmake if any files we depend on changed.
  set_property(DIRECTORY APPEND PROPERTY
    CMAKE_CONFIGURE_DEPENDS
    ${snippets_py}
    ${SNIPPET_PATHS}            #  generated variable
    )
endfunction()

zephyr_process_snippets()
