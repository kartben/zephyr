.. zephyr:code-sample:: webusb
   :name: WebUSB
   :relevant-api: usbd_api

   Receive and echo data from a web page using WebUSB API.

Overview
********

This sample demonstrates how to use the Binary Device Object Store (BOS),
Microsoft OS 2.0 descriptors, and WebUSB descriptors to implement a WebUSB
sample application. The sample USB function receives the data and echoes back
to the WebUSB API based application running in the browser on your local host.

Requirements
************

This project requires a USB device controller driver using the UDC API and a
board devicetree that provides the ``zephyr_udc0`` node label for the controller.
On your host computer, this project requires a web browser that supports the
WebUSB API, such as Chromium or a Chromium-based browser.

The sample is tested on the following boards:

* :zephyr:board:`nrf52840dk` (``nrf52840dk/nrf52840``)
* :zephyr:board:`nrf54h20dk` (``nrf54h20dk/nrf54h20/cpuapp``)
* :zephyr:board:`frdm_k64f` (``frdm_k64f``)
* :zephyr:board:`stm32f723e_disco` (``stm32f723e_disco``)
* :zephyr:board:`nucleo_f413zh` (``nucleo_f413zh``)
* :zephyr:board:`mimxrt685_evk` (``mimxrt685_evk/mimxrt685s/cm33``)
* :zephyr:board:`mimxrt1060_evk` (``mimxrt1060_evk/mimxrt1062/qspi``)
* :zephyr:board:`max32690evkit` (``max32690evkit/max32690/m4``)
* :zephyr:board:`samd21_xpro` (``samd21_xpro``)
* :zephyr:board:`same54_xpro` (``same54_xpro``)
* :zephyr:board:`sam_v71_xult` (``sam_v71_xult/samv71q21b``)

Pull request CI builds the sample for ``nrf52840dk/nrf52840`` only.

Building and Running
********************

Build and flash webusb sample with:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/usb/webusb
   :board: <board to use>
   :goals: flash
   :compact:

Demonstration
*************

The sample includes a simple WebUSB API application and can be found in the
sample directory: :zephyr_file:`samples/subsys/usb/webusb/index.html`.

There are two ways to access this sample page:

* Using browser go to :doc:`demo`

* Start a web server in the sample directory:

  .. code-block:: console

     $ python -m http.server

Then follow these steps:

#. Connect the board to your host.

#. Once the device has booted, you may see a notification from the browser: "Go
   to localhost to connect". Click on the notification to open the demo page.  If
   there is no notification from the browser, open the URL http://localhost:8001/
   in your browser.

#. Click on the :guilabel:`Connect` button to connect to the device.

#. Send some text to the device by clicking on the :guilabel:`Send` button.
   The demo application will receive the same text from the device and display
   it in the text area.

References
***********

WebUSB API Specification:
https://wicg.github.io/webusb/

Chrome for Developers, "Access USB Devices on the Web":
https://developer.chrome.com/docs/capabilities/usb
