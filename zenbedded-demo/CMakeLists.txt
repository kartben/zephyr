# Copyright (c) 2026 zenbedded contributors
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_ZENBEDDED)
  zephyr_include_directories(include)

  zephyr_library_named(zenbedded)
  zephyr_library_sources(
    lib/joint_table.c
    lib/control.c
    lib/control_thread.c
    lib/hist.c
  )
  zephyr_library_sources_ifdef(CONFIG_ZENBEDDED_PLANT_SIM  lib/plant_sim.c)
  zephyr_library_sources_ifdef(CONFIG_ZENBEDDED_PLANT_HW   lib/plant_hw.c)
endif()
