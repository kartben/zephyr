# Copyright (c) 2024 Analog Devices, Inc.
# SPDX-License-Identifier: Apache-2.0

# zephyr-keep-sorted-start
add_subdirectory_ifdef(CONFIG_GROVE_SENSORS grove)
add_subdirectory_ifdef(CONFIG_HM330X hm330x)
add_subdirectory_ifdef(CONFIG_MR60BHA2 mr60bha2)
# zephyr-keep-sorted-stop
