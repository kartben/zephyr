.. _virtio_qemu:

VIRTIO devices on QEMU boards
#############################

Several of Zephyr's QEMU board targets describe VIRTIO devices in their
devicetree, with QEMU providing the device side and the :ref:`virtio` drivers
the guest side. This page covers what they have in common: which devices exist,
what the build adds to the QEMU command line for each of them, and what has to
be running on the host.

Devices and transports
**********************

:zephyr:board:`qemu_cortex_a53` and :zephyr:board:`qemu_riscv64` reach the
devices over VIRTIO MMIO, :zephyr:board:`qemu_x86` and its variants over VIRTIO
PCI. Which transport a device sits on is fixed by the board devicetree, and the
build passes the matching ``bus=`` or ``addr=`` to QEMU, so the two cannot drift
apart:

.. list-table::
   :header-rows: 1
   :widths: 18 14 16 16 16

   * - Device
     - Node label
     - ``qemu_cortex_a53``
     - ``qemu_riscv64``
     - ``qemu_x86``
   * - Block disk
     - ``virtio_blk0``
     - ``virtio_mmio4``, on
     - ``virtio_mmio4``, on
     - ``virtio_blk_pci``
   * - GPIO controller
     - ``virtio_gpio0``
     - ``virtio_mmio5``
     - ``virtio_mmio5``
     - ``virtio_gpio_pci``
   * - I2C adapter
     - ``virtio_i2c0``
     - ``virtio_mmio6``
     - ``virtio_mmio6``
     - ``virtio_i2c_pci``

Enabling a device in the devicetree turns its driver on, and enabling the driver
is what makes the build add the device to the QEMU command line. Nothing is
added for a device an application does not use, so an unused device costs
neither guest memory nor emulator setup.

Only the block disk on the two MMIO boards is enabled out of the box, marked
"on" above. On ``qemu_x86`` nothing beyond the input device is: QEMU's q35
machine routes every PCI slot to one of two INTx lines, and a line carries a
single Zephyr ISR, so a second VIRTIO PCI device left on unconditionally would
take the interrupt away from whatever an application puts on the same line.

Block disk
**********

QEMU emulates virtio-blk itself, so all the disk needs is a backing image, which
the build creates. On the MMIO boards it is therefore enabled out of the box: an
application that turns on :kconfig:option:`CONFIG_DISK_ACCESS` - directly or
through a filesystem - gets a disk named ``VIRTIOBLK0`` with no devicetree
overlay of its own, reachable through the
:ref:`Disk Access API <disk_access_api>`. See :ref:`disk_virtio_blk` for the
driver itself. On ``qemu_x86`` the node has to be enabled first:

.. code-block:: devicetree

   &virtio_blk_pci {
           status = "okay";
   };

   &virtio_blk0 {
           status = "okay";
   };

The image is a raw file in the build directory, recreated when its size changes.
Two Kconfig options shape it:

* :kconfig:option:`CONFIG_QEMU_VIRTIO_BLK_DISK_SIZE`, the size passed to
  ``qemu-img``, ``1M`` by default.
* :kconfig:option:`CONFIG_QEMU_VIRTIO_BLK_LOGICAL_BLOCK_SIZE`, the logical block
  size the emulated device reports, 512 by default. Raising it is how the
  non-512-byte block paths get exercised.

An application that wants a different disk instead can turn this one off with
``CONFIG_DISK_DRIVER_VIRTIO_BLK=n``.

GPIO controller and I2C adapter
*******************************

QEMU emulates neither of these, so both are vhost-user devices: QEMU forwards
the virtqueues over a Unix socket to a backend process on the host, which is
what actually drives the lines. rust-vmm's `vhost-device`_ daemons are one such
backend. Because the backend maps the guest memory to reach the virtqueues, the
build switches QEMU over to a shared ``memfd`` backing when either device is in
use, which restricts both to Linux hosts.

Both are therefore disabled by default: QEMU refuses to start if nothing is
listening on the socket. Enable the one you need in an overlay, transport node
included:

.. code-block:: devicetree

   /* qemu_cortex_a53 and qemu_riscv64 */
   &virtio_mmio5 {
           status = "okay";
   };

   &virtio_gpio0 {
           status = "okay";
   };

.. code-block:: devicetree

   /* qemu_x86 */
   &virtio_gpio_pci {
           status = "okay";
   };

   &virtio_gpio0 {
           status = "okay";
   };

Start the backend before QEMU, listening on the socket the build tells QEMU to
connect to, named by :kconfig:option:`CONFIG_QEMU_VHOST_USER_GPIO_SOCKET` and
:kconfig:option:`CONFIG_QEMU_VHOST_USER_I2C_SOCKET`. These daemons append the
guest index to their ``--socket-path``, which is where the trailing ``0`` in the
defaults comes from:

.. code-block:: console

   # eight simulated GPIO lines
   vhost-device-gpio --socket-path /tmp/vhost-gpio.sock --socket-count 1 --device-list s8

   # host bus /dev/i2c-0, one client at address 0x20
   vhost-device-i2c --socket-path /tmp/vhost-i2c.sock --socket-count 1 --device-list 0:32

Every operation on either device is a round trip to the backend, so both APIs
can only be used from a thread.

LED and button
==============

Each board puts an ``led0`` and an ``sw0`` on lines 0 and 1 of its GPIO
controller, so the :zephyr:code-sample:`blinky` and :zephyr:code-sample:`button`
samples run against a GPIO backend once the controller, the ``leds`` or
``buttons`` node and the line itself are enabled. The button needs a backend
that offers ``VIRTIO_GPIO_F_IRQ``; without it the line can still be read, but no
edge is reported.

I2C shields
===========

The adapter also carries the ``zephyr_i2c`` node label, so an I2C
:ref:`shield <shields>` can be built against these boards. Whether it does
anything useful depends on the backend: the peripherals the shield describes
have to exist on the host bus the daemon was pointed at.

How the build wires it up
*************************

The QEMU command line is assembled by the fragments under
:zephyr_file:`cmake/emu/qemu`, which pick the PCI or MMIO flavour of each device
from :kconfig:option:`CONFIG_VIRTIO_PCI`. A board says where its devices go by
setting, before it includes the common QEMU board file, the QEMU device property
that selects the transport:

.. code-block:: cmake

   set(QEMU_VIRTIO_BLK_TRANSPORT bus=virtio-mmio-bus.4)
   set(QEMU_VIRTIO_GPIO_TRANSPORT bus=virtio-mmio-bus.5)
   set(QEMU_VIRTIO_I2C_TRANSPORT bus=virtio-mmio-bus.6)

A board also has to set ``QEMU_MEMORY_SIZE_MB`` to the size it passes to
``-m``, which is what sizes the shared memory backing the vhost-user devices
need.

On PCI the slot decides which interrupt line the firmware routes the device to,
so the ``addr=`` a board pins a device to and the ``interrupts`` of the matching
``virtio,pci`` node have to be kept in step.

.. _vhost-device: https://github.com/rust-vmm/vhost-device
