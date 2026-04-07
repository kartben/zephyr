.. zephyr:code-sample:: dfrobot-ai10-face
   :name: DFRobot AI10 face recognition
   :relevant-api: biometrics_interface

   Use the DFRobot AI10 (SEN0677) vision module for face enrollment and matching
   using the Zephyr biometrics API.

Overview
********

This sample exercises enrollment, template listing, verification, identification,
and deletion on the `DFRobot AI10`_ (UART). The module baud rate is fixed at **115200 8N1**.

.. _DFRobot AI10: https://github.com/DFRobot/DFRobot_AI10

User IDs
********

The module assigns **UIDs** internally. The value passed to :c:func:`biometric_enroll_start`
is only used to build the enrollment **name** string (``uNNNN``). After enrollment, call
:c:func:`biometric_template_list` to read the **UIDs** returned by the module, and use those
UIDs with :c:func:`biometric_match` (verify) and :c:func:`biometric_template_delete`.

The minimal driver treats recognition as **face-only** when the reported UID is ≤ 1000.

Driver-specific API
*******************

``#include <zephyr/drivers/biometrics_dfrobot_ai10.h>`` adds optional helpers on top of the
generic biometrics API:

- :c:func:`dfrobot_ai10_user_info_get` — read the stored **user name** and **admin** flag for a
  UID (vendor ``GET_USER_INFO``).
- :c:func:`dfrobot_ai10_enroll_options_set` / :c:func:`dfrobot_ai10_enroll_options_clear` —
  set the display name and admin role for the **next** :c:func:`biometric_enroll_capture` only.

The sample calls :c:func:`dfrobot_ai10_user_info_get` after listing templates to demonstrate
linkage.

Requirements
************

- DFRobot AI10 module (SEN0677) wired to a UART on your board (3.3 V logic, cross TX/RX).
- Board overlay defining the ``dfrobot,ai10`` node and ``biometrics`` alias (see below).

Building and Running
**********************

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/biometrics/dfrobot_ai10
   :board: nrf52840dk/nrf52840
   :goals: build flash
   :compact:

Sample Output
*************

*(Exact output depends on hardware and module state.)*

.. code-block:: console

   [00:00:00.100,000] <inf> main: DFRobot AI10 face sample (UART 115200 8N1)
   ...

Device Tree Configuration
*************************

Add an alias ``biometrics`` pointing at the ``dfrobot,ai10`` child of the UART used for the
module. Example (see ``boards/nrf52840dk_nrf52840.overlay`` in this sample):

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

Enrollment timeout (module result 0x0d)
***************************************

The module returns **0x0d** (capture timeout) if no acceptable face appears before the
**seconds** value sent in the enroll command (vendor range 3–20 s). That value is derived
from the :c:func:`biometric_enroll_capture` ``timeout`` argument, not from a hidden 5 s cap.
Use a generous ``K_SECONDS(...)``, good lighting, and face the camera within the window.

``CONFIG_BIOMETRICS_LOG_LEVEL_DBG=y`` shows TX/RX frames; a successful enroll ends with a
RELAY whose inner **result** byte is **0x00**.
