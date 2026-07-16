.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. zephyr:code-sample:: dfrobot_ai10
   :name: DFRobot AI10 face and palm recognition
   :relevant-api: biometrics_interface

   Exercise the multimodal features of the DFRobot AI10 face and palm-vein
   recognition module.

Overview
********

This sample demonstrates the features that are specific to the DFRobot AI10
(e.g. SEN0677) module on top of the generic biometrics API:

- The :c:member:`biometric_capabilities.supported_modalities` bitmask, which
  reports both :c:enumerator:`BIOMETRIC_TYPE_FACE` and
  :c:enumerator:`BIOMETRIC_TYPE_PALM` for this multimodal module.
- Single-capture enrollment: the module needs only one capture per template,
  and enrolls whichever biometric is presented (a face or a palm).
- Module-assigned user IDs: the ID passed to :c:func:`biometric_enroll_start`
  only labels the enrollment. The module allocates the actual user ID itself
  (1-1000 for faces, 1001-2000 for palm veins) and the sample reads it back
  through the device specific ``DFROBOT_AI10_ATTR_LAST_ENROLL_UID`` attribute.
- Modality reporting: :c:member:`biometric_capture_result.modality` and
  :c:member:`biometric_match_result.modality` indicate whether a face or a
  palm produced each enrollment and match, while the modality of listed
  templates is derived from the module's user ID ranges.
- The QR code corner case: when a QR code is presented during a match, the
  driver returns ``-ENOTSUP`` instead of a biometric match.

The sample enrolls a face and a palm, lists the stored templates with their
modality, identifies whichever biometric is presented next (1:N search),
verifies the enrolled face (1:1 match), and finally deletes the palm template.

Requirements
************

- DFRobot AI10 face and palm-vein recognition module (e.g. SEN0677) connected
  to a UART. The module's baud rate is fixed at 115200.

Building and Running
********************

Add the module to your board's devicetree with the ``ai10`` alias, as shown in
:zephyr_file:`samples/drivers/biometrics/dfrobot_ai10/app.overlay`, then build
and flash:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/biometrics/dfrobot_ai10
   :board: <your_board>
   :goals: build flash
   :compact:

Sample Output
*************

.. code-block:: console

   [00:00:00.512,000] <inf> main: DFRobot AI10 face and palm recognition sample
   [00:00:00.512,000] <inf> main: Module capabilities:
   [00:00:00.512,000] <inf> main:   Supported modalities:
   [00:00:00.512,000] <inf> main:     - face
   [00:00:00.512,000] <inf> main:     - palm
   [00:00:00.512,000] <inf> main:   Max templates: 2000 (faces: user IDs 1-1000, palms: 1001-2000)
   [00:00:00.512,000] <inf> main:   Samples per enrollment: 1
   [00:00:00.512,000] <inf> main: Enrollment 1: look at the camera to enroll your FACE...
   [00:00:03.641,000] <inf> dfrobot_ai10: Enrolled face as user ID 3
   [00:00:03.641,000] <inf> main: Module assigned user ID 3 (a face template), requested ID 1 was only a label
   [00:00:03.642,000] <inf> main: Enrollment 2: hold your PALM over the module...
   [00:00:09.113,000] <inf> dfrobot_ai10: Enrolled palm as user ID 1002
   [00:00:09.113,000] <inf> main: Module assigned user ID 1002 (a palm template), requested ID 2 was only a label
   [00:00:09.310,000] <inf> main: 2 stored template(s):
   [00:00:09.310,000] <inf> main:   - user ID 3 (face)
   [00:00:09.310,000] <inf> main:   - user ID 1002 (palm)
   [00:00:09.311,000] <inf> main: Identification: show your face or palm...
   [00:00:12.905,000] <inf> main: Recognized user ID 1002 by palm
   [00:00:12.906,000] <inf> main: Verification against user ID 3 (face): show your face...
   [00:00:15.230,000] <inf> main: Verified user ID 3
   [00:00:15.480,000] <inf> main: Deleted user ID 1002
   [00:00:15.671,000] <inf> main: 1 stored template(s):
   [00:00:15.671,000] <inf> main:   - user ID 3 (face)
   [00:00:15.671,000] <inf> main: Sample complete
