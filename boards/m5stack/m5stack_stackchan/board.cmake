# SPDX-License-Identifier: Apache-2.0

board_runner_args(esp32 "--esp-flash-bootloader=${CONFIG_BOOTLOADER_ESP_IDF_FILE}")
board_runner_args(esp32 "--esp-flash-partition_table=${CONFIG_ESP_PARTITION_TABLE_BIN_FILE}")
board_runner_args(esp32 "--esp-boot-address=${CONFIG_ESP_IMAGE_OFFSET}")

include(${ZEPHYR_BASE}/boards/common/esp32.board.cmake)
