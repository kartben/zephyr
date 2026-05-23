# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

if(DEFINED BOARD_REVISION AND NOT BOARD_REVISION STREQUAL "plus")
  message(FATAL_ERROR "Invalid board revision, ${BOARD_REVISION}, valid revisions are: <none> (standard T-Deck), plus")
endif()
