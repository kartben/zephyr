.. _snippet-virtio-gpu:

VIRTIO GPU Snippet (virtio-gpu)
###############################

.. code-block:: console

   west build -S virtio-gpu -b qemu_x86 samples/subsys/display/lvgl
   west build -t run

Overview
********

This snippet selects the VIRTIO GPU 2D display driver instead of QEMU ramfb.
It currently supports the :zephyr:board:`qemu_x86` and
:zephyr:board:`qemu_x86_64` boards.

The driver uses a fixed scanout configured in devicetree and exposes an
ARGB8888 framebuffer through Zephyr's display API. Cursor queues, display
hotplug, EDID, virgl, and blob resources are not supported.
