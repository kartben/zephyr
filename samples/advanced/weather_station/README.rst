.. zephyr:code-sample:: advanced-weather-station
   :name: Advanced Weather Station
   :relevant-api: display_interface sensor_interface rtc_interface bt_gatt bluetooth

   Display temperature/humidity and time in a responsive LVGL dashboard,
   with optional Bluetooth CTS and ESS services.

Overview
********

This sample demonstrates a configurable weather station that integrates
multiple subsystems:

* :ref:`sensor_api` for temperature and humidity data
* :ref:`rtc_api` for wall-clock date/time when available
* :ref:`lvgl_api` for a responsive user interface
* :ref:`bluetooth_api` (optional) for standard CTS and ESS GATT services

The sample expects a temperature/humidity sensor under the ``dht0`` alias.
When an RTC alias (``rtc``) is available and readable, the UI shows RTC date
and time. Otherwise, the UI shows uptime time since boot.

UI Behavior
***********

The dashboard uses a card layout with a clock header, temperature card,
humidity card, and status card.

* Portrait (small width): cards are stacked vertically.
* Landscape (large width): temperature and humidity cards are shown side-by-side.

The layout switch threshold is controlled by
:kconfig:option:`CONFIG_SAMPLE_WEATHER_UI_LANDSCAPE_BREAKPOINT`.

Bluetooth Behavior (Optional)
*****************************

When built with ``overlay-bt.conf``, the sample advertises:

* Current Time Service (CTS, UUID ``0x1805``)
* Environmental Sensing Service (ESS, UUID ``0x181A``)
  * Temperature characteristic (UUID ``0x2A6E``)
  * Humidity characteristic (UUID ``0x2A6F``)

If no RTC data is available, CTS uses an uptime-based synthetic time source.

Requirements
************

* A board with a display (or :zephyr:board:`native_sim <native_sim>`)
* A sensor mapped to ``dht0`` alias that provides:

  * :c:enumerator:`SENSOR_CHAN_AMBIENT_TEMP`
  * :c:enumerator:`SENSOR_CHAN_HUMIDITY`

* Optional: a board with Bluetooth LE support for CTS/ESS mode

Building and Running
********************

Base build (no Bluetooth):

.. zephyr-app-commands::
   :zephyr-app: samples/advanced/weather_station
   :host-os: unix
   :board: native_sim/native/64
   :goals: build run
   :compact:

Bluetooth-enabled build:

.. zephyr-app-commands::
   :zephyr-app: samples/advanced/weather_station
   :host-os: unix
   :board: native_sim/native/64
   :gen-args: -DEXTRA_CONF_FILE=overlay-bt.conf
   :goals: build run
   :compact:

Configuration
*************

Key sample options:

* :kconfig:option:`CONFIG_SAMPLE_WEATHER_SENSOR_PERIOD_MS`
* :kconfig:option:`CONFIG_SAMPLE_WEATHER_UI_REFRESH_MS`
* :kconfig:option:`CONFIG_SAMPLE_WEATHER_UI_LANDSCAPE_BREAKPOINT`
* :kconfig:option:`CONFIG_SAMPLE_WEATHER_USE_FAHRENHEIT`
* :kconfig:option:`CONFIG_SAMPLE_WEATHER_ENABLE_BT`
* :kconfig:option:`CONFIG_SAMPLE_WEATHER_BT_NOTIFY_PERIOD_MS`

