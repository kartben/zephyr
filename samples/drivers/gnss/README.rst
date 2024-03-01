.. zephyr:code-sample:: gnss
   :name: GNSS
   :relevant-api: gnss_interface navigation

   Connect to a GNSS device to obtain time, navigation data, and satellite information.

Overview
********
This sample demonstrates how to use a GNSS device implementing the
GNSS device driver API.

Requirements
************

This sample requires a board with a GNSS device present and enabled
in the devicetree.

Testing with the native simulator and mock NMEA data
----------------------------------------------------

You may test this sample using the :ref:`native_sim <native simulator>` and ``socat`` to feed mock
NMEA data to the GNSS driver.

.. code-block:: console

    $ socat -d -d pty,raw,echo=0 pty,raw,echo=0
    2021/10/03 16:17:36 socat[123456] N PTY is /dev/pts/1
    2021/10/03 16:17:36 socat[123456] N PTY is /dev/pts/2
    2021/10/03 16:17:36 socat[123456] N starting data transfer loop with FDs [5,5] and [7,7]

.. code-block:: console

    $ echo -e "\$GNGGA,161736.000,5710.9400,N,00957.6660,E,1,14,0.85,42.372,M,40.0,M,,*5A\r\n" > /dev/pts/1

Sample Output
*************

.. code-block:: console

    gnss: gnss_info: {satellites_cnt: 14, hdop: 0.850, fix_status: GNSS_FIX, fix_quality: GNSS_SPS}
    gnss: navigation_data: {latitude: 57.162331699, longitude : 9.961104199, bearing 12.530, speed 0.25, altitude: 42.372}
    gnss: gnss_time: {hour: 16, minute: 17, millisecond 36000, month_day 3, month: 10, century_year: 23}
    gnss has fix!
    gnss: gnss_satellite: {prn: 1, snr: 30, elevation 71, azimuth 276, system: GLONASS, is_tracked: 1}
    gnss: gnss_satellite: {prn: 11, snr: 31, elevation 62, azimuth 221, system: GLONASS, is_tracked: 1}
    gnss reported 2 satellites!
