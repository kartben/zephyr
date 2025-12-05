# SPDX-License-Identifier: Apache-2.0

#[=======================================================================[.rst:
ccache
------

Compiler cache (ccache) support module.

This module enables ccache support for faster rebuilds if ccache is installed
on the system.

Variables Affecting Behavior
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. cmake:variable:: USE_CCACHE

   Set to ``0`` to explicitly disable ccache even if it is installed.
   By default, ccache will be used if it is found on the system.

#]=======================================================================]

include_guard(GLOBAL)

if(USE_CCACHE STREQUAL "0")
else()
  find_program(CCACHE_FOUND ccache)
  if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK    ccache)
  endif()
endif()
