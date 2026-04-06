.. zephyr:code-sample:: otp
   :name: One-Time Programmable (OTP) memory
   :relevant-api: otp_interface

   Read and program a provisioning record in OTP memory.

Overview
********

This sample demonstrates the :ref:`OTP driver API <otp_api>` by writing a
simple provisioning record (magic sentinel, serial number, hardware revision)
to :abbr:`OTP (One Time Programmable)` memory and reading it back.

On the first run the sample detects that the record has not yet been
written, programs it, and verifies the readback.  On subsequent runs the
sample simply reads and displays the existing record.

.. warning::

   On real OTP hardware, programming is **irreversible**.  Each bit can
   only be written once.  Use the :kconfig:option:`CONFIG_OTP_PROGRAM`
   option deliberately, and test with the OTP emulator
   (``zephyr,otp-emul``) before targeting physical hardware.

Requirements
************

A board with an OTP memory device, or any board for which an OTP emulator
overlay can be supplied (``native_sim`` is used in the examples below).
The board must expose an ``otp-0`` devicetree alias pointing at the OTP
node.

Building and Running
********************

The sample targets :zephyr:board:`native_sim` by default, which already
includes the ``zephyr,otp-emul`` emulator in its DTS:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/otp
   :host-os: unix
   :board: native_sim
   :goals: run
   :compact:

To target a board with real OTP hardware (e.g. an STM32 with ``otp_nvm``),
add a board overlay that defines the ``otp-0`` alias and select the
appropriate driver with :kconfig:option:`CONFIG_OTP`.

Sample Output
*************

First boot (no provisioning record):

.. code-block:: console

   [00:00:00.000,000] <inf> otp_sample: OTP sample on device "otp"
   [00:00:00.000,000] <inf> otp_sample: No provisioning record found. Writing one now...
   [00:00:00.000,000] <wrn> otp_sample: WARNING: On real OTP hardware this operation is irreversible!
   [00:00:00.000,000] <inf> otp_sample: Provisioning record written and verified:
   [00:00:00.000,000] <inf> otp_sample:   Magic:    OTP
   [00:00:00.000,000] <inf> otp_sample:   Serial:   SN001234
   [00:00:00.000,000] <inf> otp_sample:   HW rev:   1
   [00:00:00.000,000] <inf> otp_sample: OTP sample complete

Subsequent boots (record already present):

.. code-block:: console

   [00:00:00.000,000] <inf> otp_sample: OTP sample on device "otp"
   [00:00:00.000,000] <inf> otp_sample: Provisioning record found:
   [00:00:00.000,000] <inf> otp_sample:   Magic:    OTP
   [00:00:00.000,000] <inf> otp_sample:   Serial:   SN001234
   [00:00:00.000,000] <inf> otp_sample:   HW rev:   1
   [00:00:00.000,000] <inf> otp_sample: OTP sample complete

.. note::

   When running on ``native_sim`` the emulator stores data in RAM and the
   record is lost when the process exits.  On hardware with persistent OTP,
   the record survives power cycles.
