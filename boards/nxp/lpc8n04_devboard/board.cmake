# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd "--target=lpc8n04fhi24")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
