# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

# VIRTIO I2C adapter. QEMU emulates none, so the bus is driven by a vhost-user
# backend - rust-vmm's vhost-device-i2c, for instance - which has to be
# listening on the socket before QEMU starts.
#
# Boards say where the device goes by setting QEMU_VIRTIO_I2C_TRANSPORT to the
# device property picking their transport, "bus=virtio-mmio-bus.6" or
# "addr=08.0" for example, matching the devicetree.

if(CONFIG_I2C_VIRTIO)
  qemu_append_vhost_user_device(i2c vhosti2c
    "${CONFIG_QEMU_VHOST_USER_I2C_SOCKET}" "${QEMU_VIRTIO_I2C_TRANSPORT}")
endif()
