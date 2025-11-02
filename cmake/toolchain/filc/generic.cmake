# SPDX-License-Identifier: Apache-2.0

# Purpose of the generic.cmake is to define a generic C compiler which can be
# used for devicetree pre-processing and other pre-processing tasks which must
# be performed before the target can be determined.

set_ifndef(FILC_TOOLCHAIN_PATH "$ENV{FILC_TOOLCHAIN_PATH}")
zephyr_get(FILC_TOOLCHAIN_PATH)

if(FILC_TOOLCHAIN_PATH)
  set(TOOLCHAIN_HOME ${FILC_TOOLCHAIN_PATH}/bin/)
endif()

set(FILC_TOOLCHAIN_PATH ${FILC_TOOLCHAIN_PATH} CACHE PATH "fil-c install directory")

set(COMPILER filc)
set(BINTOOLS llvm)

# fil-c is based on LLVM/Clang, so it can potentially support newlib or picolibc.
# This is not decided by fil-c itself, but depends on libraries distributed with the installation.
# Also newlib or picolibc may be created as add-ons. Thus always stating that fil-c does not have
# newlib or picolibc would be wrong. Same with stating that fil-c has newlib or Picolibc.
# The best assumption for TOOLCHAIN_HAS_<NEWLIB|PICOLIBC> is to check for the presence of
# '_newlib_version.h' / 'picolibc' and have the default value set accordingly.
# This provides a best effort mechanism to allow developers to have the newlib C / Picolibc library
# selection available in Kconfig.
# Developers can manually indicate library support with '-DTOOLCHAIN_HAS_<NEWLIB|PICOLIBC>=<ON|OFF>'

# Support for newlib is indicated by the presence of '_newlib_version.h' in the toolchain path.
if(NOT FILC_TOOLCHAIN_PATH STREQUAL "")
  file(GLOB_RECURSE newlib_header ${FILC_TOOLCHAIN_PATH}/_newlib_version.h)
  if(newlib_header)
    set(TOOLCHAIN_HAS_NEWLIB ON CACHE BOOL "True if toolchain supports newlib")
  endif()

  # Support for picolibc is indicated by the presence of 'picolibc.h' in the toolchain path.
  file(GLOB_RECURSE picolibc_header ${FILC_TOOLCHAIN_PATH}/picolibc.h)
  if(picolibc_header)
    set(TOOLCHAIN_HAS_PICOLIBC ON CACHE BOOL "True if toolchain supports picolibc")
  endif()
endif()

message(STATUS "Found toolchain: filc (fil-c)")
