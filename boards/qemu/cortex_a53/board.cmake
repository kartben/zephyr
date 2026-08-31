# Copyright (c) 2019 Carlo Caione <ccaione@baylibre.com>
# SPDX-License-Identifier: Apache-2.0

set(SUPPORTED_EMU_PLATFORMS qemu)
set(QEMU_BINARY_SUFFIX aarch64)

set(QEMU_CPU_TYPE cortex-a53)

if(CONFIG_ARMV8_A_NS)
  set(QEMU_MACH virt,gic-version=3)
else()
  set(QEMU_MACH virt,secure=on,gic-version=3)
endif()

if(CONFIG_ENTROPY_VIRTIO)
  set(QEMU_VIRTIO_ENTROPY_FLAGS -device virtio-rng-device,bus=virtio-mmio-bus.0)
endif()

if(CONFIG_INPUT_VIRTIO)
  if(CONFIG_INPUT_VIRTIO_DEVICE_TYPE_KEYBOARD)
    set(QEMU_VIRTIO_INPUT_FLAGS -device virtio-keyboard-device,bus=virtio-mmio-bus.3)
  elseif(CONFIG_INPUT_VIRTIO_DEVICE_TYPE_TABLET)
    set(QEMU_VIRTIO_INPUT_FLAGS -device virtio-tablet-device,bus=virtio-mmio-bus.3)
  else()
    message(WARNING "No virtio input device type selected; QEMU_VIRTIO_INPUT_FLAGS will be empty")
  endif()
endif()

# MMIO transports the block, GPIO and I2C devices are attached to, matching the
# virtio_mmio nodes they hang off in the board devicetree. The devices
# themselves are added by cmake/emu/qemu/.
set(QEMU_VIRTIO_BLK_TRANSPORT bus=virtio-mmio-bus.4)
set(QEMU_VIRTIO_GPIO_TRANSPORT bus=virtio-mmio-bus.5)
set(QEMU_VIRTIO_I2C_TRANSPORT bus=virtio-mmio-bus.6)

# The QEMU default for this machine, spelled out because a vhost-user backend
# needs the guest memory size to size its shared mapping.
set(QEMU_MEMORY_SIZE_MB 128)

set(QEMU_BOARD_FLAGS
  -cpu ${QEMU_CPU_TYPE}
  -m ${QEMU_MEMORY_SIZE_MB}
  ${QEMU_VIRTIO_ENTROPY_FLAGS}
  ${QEMU_VIRTIO_INPUT_FLAGS}
  -machine ${QEMU_MACH}
  )

if(CONFIG_XIP)
  # This should be equivalent to
  #   ... -drive if=pflash,file=build/zephyr/zephyr.bin,format=raw
  # without having to pad the binary file to the FLASH size
  set(QEMU_KERNEL_OPTION
  -bios ${PROJECT_BINARY_DIR}/${CONFIG_KERNEL_BIN_NAME}.bin
  )
endif()

include(${ZEPHYR_BASE}/boards/common/qemu.board.cmake)
