# SPDX-License-Identifier: Apache-2.0
#
# Copyright (c) 2021, Nordic Semiconductor ASA

#[=======================================================================[.rst:
user_cache
----------

Configure user cache directory for persistent build data.

This module sets up a user cache directory that persists across builds to speed
up CMake configuration and compilation. The cache stores data that can be safely
regenerated, such as compiler check results.

Outcome
^^^^^^^

The following variables are defined when this module completes:

.. cmake:variable:: USER_CACHE_DIR

   The user cache directory in use. If this variable is already set when
   the module loads, it will not be modified.

Platform-Specific Cache Locations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- **macOS**: ``~/Library/Caches``
- **Windows**: ``%LOCALAPPDATA%\.cache``
- **Linux**: ``$XDG_CACHE_HOME`` or ``~/.cache``

See Also
^^^^^^^^

https://github.com/zephyrproject-rtos/zephyr/pull/7102

#]=======================================================================]

include_guard(GLOBAL)

include(python)

function(find_appropriate_cache_directory dir)
  set(env_suffix_LOCALAPPDATA   .cache)

  if(CMAKE_HOST_APPLE)
    # On macOS, ~/Library/Caches is the preferred cache directory.
    set(env_suffix_HOME Library/Caches)
  else()
    set(env_suffix_HOME .cache)
  endif()

  # Determine which env vars should be checked
  if(CMAKE_HOST_APPLE)
    set(dirs HOME)
  elseif(CMAKE_HOST_WIN32)
    set(dirs LOCALAPPDATA)
  else()
    # Assume Linux when we did not detect 'mac' or 'win'
    #
    # On Linux, freedesktop.org recommends using $XDG_CACHE_HOME if
    # that is defined and defaulting to $HOME/.cache otherwise.
    set(dirs
      XDG_CACHE_HOME
      HOME
      )
  endif()

  foreach(env_var ${dirs})
    if(DEFINED ENV{${env_var}})
      set(env_dir $ENV{${env_var}})

      string(JOIN "/" test_user_dir ${env_dir} ${env_suffix_${env_var}})

      execute_process(COMMAND ${PYTHON_EXECUTABLE}
        ${ZEPHYR_BASE}/scripts/build/dir_is_writeable.py ${test_user_dir}
        RESULT_VARIABLE writable_result
      )
      if("${writable_result}" STREQUAL "0")
        # The directory is write-able
        set(user_dir ${test_user_dir})
        break()
      else()
        # The directory was not writeable, keep looking for a suitable
        # directory
      endif()
    endif()
  endforeach()

  # Populate local_dir with a suitable directory for caching
  # files. Prefer a directory outside of the git repository because it
  # is good practice to have clean git repositories.
  if(DEFINED user_dir)
    # Zephyr's cache files go in the "zephyr" subdirectory of the
    # user's cache directory.
    set(local_dir ${user_dir}/zephyr)
  else()
    set(local_dir ${ZEPHYR_BASE}/.cache)
  endif()

  set(${dir} ${local_dir} PARENT_SCOPE)
endfunction()

# Populate USER_CACHE_DIR with a directory that user applications may
# write cache files to.
if(NOT DEFINED USER_CACHE_DIR)
  find_appropriate_cache_directory(USER_CACHE_DIR)
endif()
message(STATUS "Cache files will be written to: ${USER_CACHE_DIR}")
