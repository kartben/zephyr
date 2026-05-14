# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MESHTASTIC)

  zephyr_library_named(meshtastic)

  list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
  include(nanopb)

  # Generate C sources from the Meshtastic protobuf schema.
  zephyr_nanopb_sources(meshtastic
    proto/portnums.proto
    proto/mesh.proto
  )

  # Allow #include "mesh.pb.h" (without the proto/ prefix) in meshtastic.c.
  target_include_directories(meshtastic PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/proto)

  zephyr_library_sources(meshtastic.c)

endif()
