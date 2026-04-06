.. _usb_host_uvc:

USB Host UVC (USB Video Class)
##############################

Overview
********

The USB Host :abbr:`UVC (USB Video Class)` driver allows a Zephyr device
acting as a USB host to capture video from a connected UVC-compliant
camera.  It is built on top of the :ref:`USB host stack <uhc_api>` and
exposes frames through Zephyr's :ref:`Video driver API <video_api>`.

.. note::

   USB host UVC support is **experimental**.  The API and Kconfig symbols
   are subject to change without notice.

Architecture
************

The driver consists of two layers:

.. code-block:: text

   Application (Video API)
         │
         ▼
   USB Host UVC class driver    (usbh_uvc)
   ┌─────────────────────────────────────────┐
   │  UVC descriptor parsing                 │
   │  Format / frame negotiation             │
   │  Isochronous / Bulk ISO transfer mgmt   │
   │  Video control (brightness, contrast …) │
   └─────────────────────────────────────────┘
         │
         ▼
   USB Host Core / UHC driver   (usbh_core, uhc)
         │
         ▼
   USB Host Controller hardware

The UVC class driver is registered with the host core via a **host class
driver** descriptor (``USBH_DEFINE_CLASS``).  When the host enumerates a
device and finds an interface with class ``CC_VIDEO``, the UVC driver
claims it, parses the UVC descriptors to discover supported formats, and
makes the device available as a Zephyr video device.

Video device naming
===================

Each connected UVC camera is exposed as ``usbh_uvc_0``,
``usbh_uvc_1``, etc.  Use :c:func:`video_get_format`,
:c:func:`video_set_format`, and :c:func:`video_stream_start` from
the :ref:`Video API <video_api>` to interact with the device.

Format negotiation
==================

The driver reads the UVC **VideoStreaming** descriptors to build a
capability table of ``(pixel_format, width, height, frame_interval)``
combinations that the camera supports.  When the application calls
:c:func:`video_set_format`, the driver negotiates a UVC commit block with
the camera and selects the appropriate alternate interface (for
isochronous endpoints).

Video controls
==============

UVC Processing Unit and Camera Terminal controls are mapped to Zephyr
:ref:`Video controls <video_api>` where supported, including:

- Brightness, Contrast, Saturation
- Exposure (absolute)
- Gain
- White Balance Temperature
- Focus (absolute), Zoom (absolute)

Unsupported controls are silently skipped during enumeration.

Supported pixel formats
=======================

The following UVC payload formats are supported:

- **YUYV** (uncompressed YUV 4:2:2) — ``VIDEO_PIX_FMT_YUYV``
- **MJPEG** (JPEG-compressed) — ``VIDEO_PIX_FMT_JPEG``

Requirements
************

- A USB host controller with isochronous transfer support (e.g.
  ``rd_rw612_bga/rw612``).
- A UVC-compliant USB camera.

Kconfig options
***************

:kconfig:option:`CONFIG_USBH_UVC`
   Enable the UVC host class driver.

:kconfig:option:`CONFIG_USBH_VIDEO_CONCURRENT_TRANSFERS`
   Number of concurrent isochronous USB transfers.  Increase this to
   sustain higher frame rates.

:kconfig:option:`CONFIG_UHC_BUF_COUNT` / :kconfig:option:`CONFIG_UHC_BUF_POOL_SIZE`
   Number and total size of USB host transfer buffers.  The pool must be
   large enough to hold at least two maximum-size isochronous packets.

:kconfig:option:`CONFIG_VIDEO_BUFFER_POOL_NUM_MAX` / :kconfig:option:`CONFIG_VIDEO_BUFFER_POOL_HEAP_SIZE`
   Number and size of video frame buffers.  A minimum of four buffers is
   recommended to avoid dropped frames.

Sample
******

See :zephyr:code-sample:`usb-host-uvc` for a complete working example
that enumerates a UVC camera, negotiates a format, and logs frame
statistics.
