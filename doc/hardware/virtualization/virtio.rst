Virtual I/O (VIRTIO)
##########################

Overview
********

Virtual I/O (VIRTIO) is a protocol used for communication with various devices, typically used in
virtualized environments. Its main goal is to provide an efficient and standardized mechanism for
interfacing with virtual devices from within a virtual machine. The communication relies on virtqueues
and standard transfer methods like PCI or MMIO.

Concepts
********

Virtio defines various components used during communication and initialization. It specifies both the
host (named "device" in the specification) and guest (named "driver" in the specification) sides.
Currently Zephyr can only work as a guest. On top of the facilities exposed by the Virtio driver,
a driver for a specific device (e.g. network card) can be implemented.

A high-level overview of a system with a Virtio device is shown below.

.. graphviz::
   :caption: Virtual I/O overview

   digraph {

        subgraph cluster_host {
            style=filled;
            color=lightgrey;
            label = "Host";
            labeljust=r;

            virtio_device [label = "virtio device"];
        }

        transfer_method [label = "virtio transfer method"];

        subgraph cluster_guest {
            style=filled;
            color=lightgrey;
            label = "Guest";
            labeljust=r;

            virtio_driver [label = "virtio driver"];
            specific_device_driver [label = "specific device driver"];
            device_user [label = "device user"];
        }

        virtio_device -> transfer_method;
        transfer_method -> virtio_device;
        transfer_method -> virtio_driver;
        virtio_driver -> transfer_method;
        virtio_driver -> specific_device_driver;
        specific_device_driver -> virtio_driver;
        specific_device_driver -> device_user;
        device_user -> specific_device_driver;
   }

Configuration space
===================
Each device provides configuration space, used for initialization and configuration. It allows
selection of device and driver features, enabling specific virtqueues and setting their addresses.
Once the device is configured, most of its configuration cannot be changed without resetting the device.
The exact layout of the configuration space depends on the transfer method.

Driver and device features
--------------------------
The configuration space provides a way to negotiate feature bits, determining some non-mandatory
capabilities of the devices. The exact available feature bits depend on the device and platform.

Device-specific configuration
-----------------------------
Some of the devices offer device-specific configuration space, providing additional configuration options.

Virtqueues
==========
The main mechanism used for transferring data between host and guest is a virtqueue. Specific
devices have different numbers of virtqueues, for example devices supporting bidirectional transfer
usually have one or more tx/rx virtqueue pairs. Virtio specifies two types of virtqueues: split
virtqueues and packed virtqueues. Zephyr currently supports only split virtqueues.

Split virtqueues
----------------
A split virtqueue consists of three parts: descriptor table, available ring and used ring.

The descriptor table holds descriptors of buffers, that is their physical addresses, lengths and flags.
Each descriptor is either device writeable or driver writeable. The descriptors can be chained, creating
descriptor chains. Typically a chain begins with descriptors containing the data for the device to read
and ends with the device writeable part, where the device places its response.

The main part of the available ring is a circular buffer of references (in the form of indexes) to the
descriptors in the descriptor table. Once the guest decides to send the data to the host, it adds the index of
the head of the descriptor chain to the top of the available ring.

The used ring is similar to the available ring, but it's used by the host to return descriptors to the guest. In
addition to storing descriptor indexes, it also provides information about the amount of data written to them.

Common Virtio libraries
***********************

Zephyr provides an API for interfacing with Virtio devices and virtqueues, which allows performing necessary operations
over the lifetime of the Virtio device.

Device initialization
=====================
Once the Virtio driver finishes performing low-level initialization common to the all devices using a given transfer method,
like finding device on the bus and mapping Virtio structures, the device specific driver steps in and performs the next
stages of initialization with the help of the Virtio API.

The first thing the device-specific driver does is feature bits negotiation. It uses :c:func:`virtio_read_device_feature_bit`
to determine which features the device offers, and then selects the ones it needs using :c:func:`virtio_write_driver_feature_bit`.
After all required features have been selected, the device-specific driver calls :c:func:`virtio_commit_feature_bits`. Then, virtqueues
are initialized with :c:func:`virtio_init_virtqueues`. This function enumerates the virtqueues, invoking the provided callback
:c:type:`virtio_enumerate_queues` to determine the required size of each virtqueue. Initialization process is finalized by calling
:c:func:`virtio_finalize_init`. From this point, if none of the functions returned errors, the virtqueues are operational. If the
specific device provides one, the device-specific config can be obtained by calling :c:func:`virtio_get_device_specific_config`.

Virtqueue operation
===================
Once the virtqueues are operational, they can be used to send and receive data. To do so, the pointer to the nth
virtqueue has to be acquired using :c:func:`virtio_get_virtqueue`. To send data consisting of a descriptor chain,
:c:func:`virtq_add_buffer_chain` has to be used. Along the descriptor chain, it takes pointer to the callback that
will be invoked once the device returns the given descriptor chain. After that, the virtqueue has to be notified using
:c:func:`virtio_notify_virtqueue` from the Virtio API.

Guest-side Virtio drivers
*************************
Currently Zephyr provides drivers for Virtio over PCI and Virtio over MMIO, along with drivers for
several devices using virtio: virtiofs, used to access the filesystem of the host, virtio-entropy,
used as an entropy source, virtio-net, virtio-console, virtio-input and virtio-sound.

Virtiofs
=========
This driver provides support for `virtiofs <https://virtio-fs.gitlab.io/>`_ - a filesystem allowing a virtual machine guest to access
a directory on the host. It uses FUSE messages to communicate between the host and the guest in order to perform filesystem operations such as
opening and reading files. Every time the guest wants to perform some filesystem operation it places in the virtqueue a descriptor chain
starting with the device readable part, containing the FUSE input header and input data, and ending it with the device writeable part, with place
for the FUSE output header and output data.

Virtio-entropy
==============
This driver allows using virtio-entropy as an entropy source in Zephyr. The operation of this device is simple - the driver places a
buffer in the virtqueue and receives it back, filled with random data.

Virtio-sound
============
This driver exposes a virtio-sound device (virtio device ID 25) through the :ref:`i2s_api`. A single
devicetree node binds two virtio PCM streams: an output stream backing ``I2S_DIR_TX`` and an input
stream backing ``I2S_DIR_RX``. The streams to use are picked automatically, or can be selected
explicitly with the ``tx-stream-id`` and ``rx-stream-id`` properties, while ``stream-direction``
restricts the node to a single direction.

The ``qemu_cortex_a53``, ``qemu_riscv64``, ``qemu_x86`` and ``qemu_x86_64`` boards declare the node
themselves, under the label ``virtio_sound``, so an application only has to set
:kconfig:option:`CONFIG_I2S` and point at it, either with an alias:

.. code-block:: devicetree

   / {
   	aliases {
   		i2s-tx = &virtio_sound;
   	};
   };

or by giving it the node label the application expects:

.. code-block:: devicetree

   i2s_rxtx: &virtio_sound {
   	status = "okay";
   };

Applications that do not enable :kconfig:option:`CONFIG_I2S` are unaffected: the driver is not built,
the VIRTIO core is not pulled in, and no audio device is added to the QEMU command line.

PCM data is carried as virtio messages rather than through a shared memory region, so one I2S block
maps to exactly one virtio period: :c:func:`i2s_write` places a descriptor chain holding the transfer
header, the block and a status structure on the transmit virtqueue, and capture works the same way
with the block and the status made device writeable. The control virtqueue carries the per-stream
``SET_PARAMS``, ``PREPARE``, ``START``, ``STOP`` and ``RELEASE`` requests that back
:c:func:`i2s_configure` and :c:func:`i2s_trigger`.

A few things are worth keeping in mind when using this driver:

* The memory slab passed to :c:func:`i2s_configure` must be statically defined. Descriptor addresses
  are translated with :c:func:`k_mem_phys_addr`, which requires the kernel's permanent RAM mapping.
* :c:func:`i2s_configure` and :c:func:`i2s_trigger` block on the control virtqueue and therefore have
  to be called from thread context.
* ``I2S_OPT_LOOPBACK`` and ``I2S_OPT_PINGPONG`` are rejected, and the clock related fields of
  :c:struct:`i2s_config` are validated then ignored, since a virtual device has no clock lines.
* ``I2S_TRIGGER_STOP`` behaves like ``I2S_TRIGGER_DRAIN`` on the transmit direction. Once a stream is
  running, every block the application wrote is already owned by the device, and virtio-sound has no
  request to stop after the current period: ``PCM_STOP`` pauses without returning buffers and only
  ``PCM_RELEASE`` returns them. Draining is the only lossless mapping.
* Jack, channel map and mixer control requests are not implemented.

Because one I2S block is one virtio period, ``block_size`` decides how often the device has to be
fed. Host audio backends work on their own timer, 10 ms in the QEMU case, so a block much shorter
than that leaves the backend short of data on every tick and the output comes out chopped into
audible pulses. This is a real difference from a hardware I2S controller, where a DMA engine clocks
samples out continuously and periods of a millisecond or two are unremarkable. Aim for blocks of
about 10 ms or more, keep several of them queued ahead of the device, and make sure the memory slab
is large enough that blocks actually circulate rather than all being held by the application. The
driver logs the negotiated period and buffer size in milliseconds at debug level, and warns once per
run if the transmit stream runs out of queued blocks.

Running under QEMU requires QEMU 8.2 or later. The device cannot be instantiated without an audio
backend, so :kconfig:option:`CONFIG_QEMU_AUDIODEV` selects the one passed to ``-audiodev``. It
defaults to ``none``, a timer driven null backend that is always available and exercises the whole
driver path without touching host audio. Set it to ``wav`` to capture playback to a file, or to a
host backend such as ``pa``, ``alsa`` or ``coreaudio`` to actually hear it.

The quickest way to hear something is the :kconfig:option:`CONFIG_I2S_SHELL` test tone generator,
which needs no application code at all:

.. code-block:: console

   west build -b qemu_cortex_a53 samples/subsys/shell/shell_module -- \
       -DCONFIG_I2S=y -DCONFIG_I2S_SHELL=y \
       -DCONFIG_I2S_SHELL_BLOCK_FRAMES=512 \
       -DCONFIG_QEMU_AUDIODEV=\"coreaudio\"
   west build -t run

Then, at the shell prompt, stream a 440 Hz tone until you stop it:

.. code-block:: console

   uart:~$ i2s tone start virtio-sound 440 48000 16
   uart:~$ i2s tone stop

``CONFIG_I2S_SHELL_BLOCK_FRAMES`` is raised from its default of 64 on purpose. At 48 kHz stereo that
default is a 1.33 ms block, and with the shell's eight blocks it leaves only about 10 ms of audio in
the device, right at the QEMU backend's timer period. 512 frames gives an 85 ms cushion and a clean
tone. Substitute ``wav`` for ``coreaudio`` to capture to ``qemu.wav`` instead of playing live, and
exit QEMU with :kbd:`Ctrl-A` :kbd:`x` so the file is closed properly.

The same device also backs :zephyr:code-sample:`i2s-output` and :zephyr:code-sample:`i2s-echo` on
these boards, though both use small blocks by default and are better suited to checking the API than
to listening.

Virtio samples
**************
A sample showcasing the use of a driver relying on Virtio is provided in :zephyr:code-sample:`virtiofs`. If you wish
to check code interfacing directly with the Virtio driver, you can check the virtiofs driver, especially :c:func:`virtiofs_init`
for initialization and :c:func:`virtiofs_send_receive` with the :c:func:`virtiofs_recv_cb` for data transfer to/from
the Virtio device.

API Reference
*************

.. doxygengroup:: virtio_interface
.. doxygengroup:: virtqueue_interface
