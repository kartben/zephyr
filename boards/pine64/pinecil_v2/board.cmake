# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

# BL706 boards use the bl702 chip profile with bflb_mcu_tool.
board_runner_args(bflb_mcu_tool --chipname bl702)
include(${ZEPHYR_BASE}/boards/common/bflb_mcu_tool.board.cmake)

board_set_flasher(bflb_mcu_tool)
