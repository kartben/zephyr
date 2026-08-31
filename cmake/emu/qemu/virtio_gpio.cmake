# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

# VIRTIO GPIO device. QEMU emulates none, so the lines are driven by a
# vhost-user backend - rust-vmm's vhost-device-gpio, for instance - which has
# to be listening on the socket before QEMU starts.
#
# Boards say where the device goes by setting QEMU_VIRTIO_GPIO_TRANSPORT to the
# device property picking their transport, "bus=virtio-mmio-bus.5" or
# "addr=07.0" for example, matching the devicetree.

if(CONFIG_GPIO_VIRTIO)
  qemu_append_vhost_user_device(gpio vhostgpio
    "${CONFIG_QEMU_VHOST_USER_GPIO_SOCKET}" "${QEMU_VIRTIO_GPIO_TRANSPORT}")
endif()
