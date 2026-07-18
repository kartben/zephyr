# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(agent_pad)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
target_sources(app PRIVATE
  src/main.c
  src/state.c
  src/keymap.c
  src/link.c
  src/ui.c
)

if(CONFIG_AGENT_PAD_SIM)
  target_sources(app PRIVATE src/sim.c)
else()
  target_sources(app PRIVATE src/hid_kb.c src/leds.c src/chime.c)
endif()
