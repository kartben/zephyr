.. zephyr:code-sample:: biometric-station
   :name: Biometric Station
   :relevant-api: biometrics_interface display_interface

   LVGL kiosk-style enrollment and identification demo for ``qemu_x86`` using the
   generic biometrics API and the in-tree biometrics emulator.

Overview
********

``biometric_station`` is a presentation-style demo for Zephyr's biometrics subsystem.
It runs a polished LVGL flow on the :zephyr:board:`qemu_x86` RAM framebuffer at
**1024x768**, guides the user through automatic enrollment when no identity is stored,
then performs an identification pass and stops on a clear result screen.

The sample stays on the generic :ref:`biometrics_interface`, so the same application
logic can later be pointed at supported hardware by changing only devicetree.

Features
********

- Splash and prompt screens tailored for a kiosk-style experience
- Optional automatic enrollment when the biometrics database is empty
- Animated scan feedback during identification
- AI10-specific name lookup on successful identification when the active sensor is
  ``dfrobot,ai10``
- Success, no-match, timeout, and generic error result states
- Optional source-local emulator overrides for manual negative-path checks

Requirements
************

- :zephyr:board:`qemu_x86`
- A working QEMU display backend so the RAM framebuffer window is visible

Building and Running
********************

Build the default ``qemu_x86`` demo:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/biometrics/biometric_station
   :board: qemu_x86
   :goals: build run
   :compact:

Exit QEMU with :kbd:`CTRL+A` followed by :kbd:`x`.

Behavior
********

On startup the sample checks whether the biometrics device already has a stored
template:

- If no template is present, the app starts guided enrollment automatically.
- When enrollment completes, the app transitions to an ``Identify yourself`` prompt.
- After a short delay, the app starts identification automatically.
- The final result screen remains visible until the application exits or resets.

The worker thread performs blocking biometrics calls, while the main thread owns
LVGL state and updates the UI from queued events.

Kconfig Options
***************

The sample exposes one private switch:

- ``CONFIG_BIOMETRIC_STATION_ENABLE_ENROLL``: enables the enrollment flow and its
  associated UI. When disabled, the demo becomes an identify-only station and skips
  the enrollment screen and stage markers entirely.

Example identify-only build:

.. code-block:: console

   printf 'CONFIG_BIOMETRIC_STATION_ENABLE_ENROLL=n\n' > /tmp/biometric-station-identify.conf
   west build -b qemu_x86 samples/drivers/biometrics/biometric_station -- \
     -DEXTRA_CONF_FILE=/tmp/biometric-station-identify.conf

Manual Failure and Timeout Checks
*********************************

The source file contains a local emulator behavior switch for manual QEMU checks:

- default success path
- no-match result
- timeout result

This is intentionally source-local so the sample stays simple and does not add new
Kconfig surface.

Using DFRobot AI10 Later
************************

To move from the emulator to a DFRobot AI10 module later, keep the application code
unchanged and update only devicetree so the ``biometrics`` alias points at a
``dfrobot,ai10`` child on the UART connected to the module.

Example:

.. code-block:: devicetree

   / {
       aliases {
           biometrics = &dfrobot_ai10;
       };
   };

   &uart1 {
       status = "okay";
       current-speed = <115200>;

       dfrobot_ai10: dfrobot_ai10 {
           compatible = "dfrobot,ai10";
           status = "okay";
       };
   };

The DFRobot AI10 sample at :zephyr:code-sample:`dfrobot-ai10-face` shows the
device-specific setup and behavior in more detail. That driver also exposes
``#include <zephyr/drivers/biometrics_dfrobot_ai10.h>`` with
:c:func:`dfrobot_ai10_user_info_get`, which reads the stored display name and
admin flag for a module UID. This metadata is vendor-specific and should not be
obtained by casting generic biometric template data to a custom struct.

Board-Specific UART Notes
*************************

Different boards expose their external UART pins differently. Two boards that both
have a ``uart1`` node in devicetree do not necessarily route that UART to the same
header or connector.

On ``nrf7002dk/nrf5340/cpuapp`` specifically:

- the external Arduino serial port is ``&arduino_serial`` / ``&uart1``
- **TX** is Arduino ``D1`` / ``P1.01``
- **RX** is Arduino ``D0`` / ``P1.00``
- the on-board USB debug console is on ``uart0``, not on the Arduino serial pins

This means a wiring setup that worked on another board, or that is attached to the
USB VCOM/debug port, can still fail on nRF7002 DK even when the UART peripheral is
configured correctly in software.

The sample includes a ready-made overlay for ``nrf7002dk/nrf5340/cpuapp`` at
``boards/nrf7002dk_nrf5340_cpuapp.overlay``.
