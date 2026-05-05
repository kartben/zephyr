# SPDX-License-Identifier: Apache-2.0

zephyr_library_include_directories(.)
zephyr_include_directories(${ZEPHYR_BASE}/subsys/net/lib/sockets)

zephyr_library_sources_ifdef(CONFIG_WEBRTC
  webrtc.c
  webrtc_sdp.c
  webrtc_ice.c
  webrtc_dtls.c
  webrtc_datachannel.c
)

zephyr_library_link_libraries_ifdef(CONFIG_MBEDTLS mbedTLS)
