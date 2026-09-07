# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sensor_regmap)

if(CONFIG_GENERIC_SENSOR_TEST AND CONFIG_TEST_SENSOR_REGMAP_BASELINE)
  target_sources(app PRIVATE src/baseline.c)
elseif(CONFIG_GENERIC_SENSOR_TEST)
  target_sources(app PRIVATE ../../build_all/sensor/src/generic_test.c)
else()
  target_sources(app PRIVATE src/main.c src/registers.c)
endif()
