# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_include_directories(
  ${ZEPHYR_BASE}/subsys/net/lib/sockets
)

zephyr_library_sources(
  rtl8720_erpc.c
  rtl8720_wifi.c
)

zephyr_library_sources_ifdef(CONFIG_NET_SOCKETS_OFFLOAD rtl8720_socket.c)
