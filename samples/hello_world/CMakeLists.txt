# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

target_sources(app PRIVATE src/main.c)


   include(git)

   git_describe(${CMAKE_CURRENT_SOURCE_DIR} GIT_DESCRIPTION)
   if(DEFINED GIT_DESCRIPTION)
     message(STATUS "Git description: ${GIT_DESCRIPTION}")
   endif()

