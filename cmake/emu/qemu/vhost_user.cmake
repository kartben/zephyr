# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

# Helper for the VIRTIO devices QEMU has no model of its own for. Such a device
# is a vhost-user device: QEMU forwards its virtqueues over a Unix socket to a
# backend process on the host, which is what implements the device.

# Add the vhost-user VIRTIO <device>, connected to the backend listening on
# <socket> and attached to the transport selected by <transport>, a QEMU device
# property such as "bus=virtio-mmio-bus.5" or "addr=07.0". <device> names the
# QEMU device type without its transport suffix, so "gpio" stands for
# vhost-user-gpio-pci or vhost-user-gpio-device, whichever the build uses.
# <chardev> names the chardev on the command line and has to be unique.
function(qemu_append_vhost_user_device device chardev socket transport)
  if(NOT QEMU_MEMORY_SIZE_MB)
    message(FATAL_ERROR
      "The vhost-user VIRTIO ${device} device needs the guest memory shared "
      "with its backend process, which this board does not support: it does "
      "not set QEMU_MEMORY_SIZE_MB.")
  endif()

  # The backend maps the guest memory to reach the virtqueues, so it has to be
  # shared rather than anonymous. One backing serves every vhost-user device.
  get_property(shared GLOBAL PROPERTY ZEPHYR_QEMU_VHOST_USER_MEMORY)
  if(NOT shared)
    qemu_append_flags(
      -object memory-backend-memfd,id=vhostmem,size=${QEMU_MEMORY_SIZE_MB}M,share=on
      -machine memory-backend=vhostmem
    )
    set_property(GLOBAL PROPERTY ZEPHYR_QEMU_VHOST_USER_MEMORY TRUE)
  endif()

  if(CONFIG_VIRTIO_PCI)
    set(dev "vhost-user-${device}-pci,chardev=${chardev}")
  else()
    set(dev "vhost-user-${device}-device,chardev=${chardev}")
  endif()

  if(transport)
    string(APPEND dev ",${transport}")
  endif()

  qemu_append_flags(
    -chardev socket,path=${socket},id=${chardev}
    -device ${dev}
  )
endfunction()
