.. zephyr:code-sample:: usb-cdc-acm
   :name: USB CDC ACM UART sample
   :relevant-api: usbd_api uart_interface

   Use USB CDC ACM UART driver to implement a serial port echo.

Overview
********

This sample app demonstrates use of a USB Communication Device Class (CDC)
Abstract Control Model (ACM) driver provided by the Zephyr project.
Received data from the serial port is echoed back to the same port
provided by this driver.

Requirements
************

This project requires a USB device controller driver (UDC API) and a board
devicetree that provides the ``zephyr_udc0`` node label for the controller.
The sample is tested on the following boards:

* :zephyr:board:`nrf52840dk` (``nrf52840dk/nrf52840``)
* :zephyr:board:`nrf54h20dk` (``nrf54h20dk/nrf54h20/cpuapp``)
* :zephyr:board:`frdm_k64f` (``frdm_k64f``)
* :zephyr:board:`stm32f723e_disco` (``stm32f723e_disco``)
* :zephyr:board:`nucleo_f413zh` (``nucleo_f413zh``)
* :zephyr:board:`mimxrt685_evk` (``mimxrt685_evk/mimxrt685s/cm33``)
* :zephyr:board:`mimxrt1060_evk` (``mimxrt1060_evk/mimxrt1062/qspi``)
* :zephyr:board:`max32690evkit` (``max32690evkit/max32690/m4``)
* :zephyr:board:`sam4e_xpro` (``sam4e_xpro``)
* :zephyr:board:`sam4l_ek` (``sam4l_ek``)
* :zephyr:board:`samd21_xpro` (``samd21_xpro``)
* :zephyr:board:`same54_xpro` (``same54_xpro``)
* :zephyr:board:`sam_v71_xult` (``sam_v71_xult/samv71q21b``)

The ``cdc-acm-workqueue`` test scenario runs on ``frdm_k64f`` only. Pull
request CI builds the sample for ``nrf52840dk/nrf52840`` and the
``cdc-acm-workqueue`` scenario for ``frdm_k64f``.

Building and Running
********************

nRF52840 DK
===========

To see the console output of the app, open a serial port emulator and
attach it to the board's console UART. Build and flash the project:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/usb/cdc_acm
   :board: nrf52840dk/nrf52840
   :goals: flash
   :compact:

Running
=======

Plug the board into a host device, for example, a PC running Linux.
The board will be detected as shown by the Linux dmesg command:

.. code-block:: console

   usb 9-1: new full-speed USB device number 112 using uhci_hcd
   usb 9-1: New USB device found, idVendor=8086, idProduct=f8a1
   usb 9-1: New USB device strings: Mfr=1, Product=2, SerialNumber=3
   usb 9-1: Product: CDC-ACM
   usb 9-1: Manufacturer: Intel
   usb 9-1: SerialNumber: 00.01
   cdc_acm 9-1:1.0: ttyACM1: USB ACM device

The app prints on the console UART:

.. code-block:: console

   Wait for DTR

Open a serial port emulator, for example minicom
and attach it to detected CDC ACM device:

.. code-block:: console

   minicom --device /dev/ttyACM1

The app should respond on serial output with:

.. code-block:: console

   DTR set, start test
   Baudrate detected: 115200

And on ttyACM device, provided by zephyr USB device stack:

.. code-block:: console

   Send characters to the UART device
   Characters read:

The characters entered in serial port emulator will be echoed back.

Troubleshooting
===============

If the ModemManager runs on your operating system, it will try
to access the CDC ACM device and maybe you can see several characters
including "AT" on the terminal attached to the CDC ACM device.
You can add or extend the udev rule for your board to inform
ModemManager to skip the CDC ACM device.
For this example, it would look like this:

.. code-block:: none

   ATTRS{idVendor}=="8086" ATTRS{idProduct}=="f8a1", ENV{ID_MM_DEVICE_IGNORE}="1"
