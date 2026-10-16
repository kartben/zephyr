# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/dshot.h)

zephyr_library()

zephyr_library_sources(dshot_command.c)
zephyr_library_sources_ifdef(CONFIG_DSHOT_PULSE_IO
  dshot_pulse_io.c
  dshot_pulse_io_wave.c
)
zephyr_library_sources_ifdef(CONFIG_USERSPACE dshot_handlers.c)
