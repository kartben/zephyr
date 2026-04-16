.. zephyr:code-sample:: display_six_rects
   :name: Six color bands (display)
   :relevant-api: display_interface

Overview
********

Fills the screen with six horizontal stripes (top to bottom on UC8159C /
Spectra 6: yellow, red, green, blue, black, white — indices per
:c:file:`uc8159c.h`). Useful for checking multi-color EPD panels (e.g. GDEP073E01
or AC057TC1).

Requirements
************

* Devicetree chosen ``zephyr,display`` set to a supported controller.

* Enough heap for a full framebuffer (see ``CONFIG_HEAP_MEM_POOL_SIZE`` in
  :file:`prj.conf`). Reduce resolution or increase heap on constrained boards.

Supported pixel formats
***********************

* ``PIXEL_FORMAT_UC8159C`` — :c:macro:`UC8159C_PALETTE_*` in :file:`uc8159c.h`
  (solid primaries). Do not use :c:macro:`UC8159C_KPIX_*` for that: those are
  datasheet LUT **row** labels and often look **washed** when used as fill colors.
* ``PIXEL_FORMAT_L_4`` — AC057TC1 4-bit indices (see :file:`ac057tc1.h`).
* ``PIXEL_FORMAT_RGB_565`` — six saturated RGB565 colors.
* ``PIXEL_FORMAT_MONO10`` — six gray / fill patterns.

E Ink (e.g. Spectra 6) is **less saturated** than backlit LCDs, and refresh
waveforms can soften band edges compared to “perfect” rectangles on glass.

Building and running
**********************

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/display_six_rects
   :board: reterminal_e1002/esp32s3/procpu
   :goals: build flash
   :compact:
