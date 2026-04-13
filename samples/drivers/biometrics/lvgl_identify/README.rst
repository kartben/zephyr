.. zephyr:code-sample:: lvgl-biometric-identify
   :name: LVGL biometric identify (QEMU + emul)
   :relevant-api: biometrics_interface

   Fully automatic identification UI on QEMU ramfb (1024×768) with the in-tree biometrics
   emulator; the same application targets real sensors via devicetree. **No enrollment** and **no user input** (no touch, buttons, or keyboard).

Overview
********

The UI **only runs identification** against templates already stored on the sensor. It cycles
automatically:

#. Idle screen with short animation
#. Busy / spinner while :c:func:`biometric_match` runs in ``BIOMETRIC_MATCH_IDENTIFY`` mode
#. Result (OK, not recognized, timeout, or error) for a few seconds
#. Back to idle, then the next round

There are **no interactive controls** — suitable for QEMU and headless-style demos where input
devices are unavailable.

If **no templates** exist (typical for the emulated sensor on first boot), the app shows a static
explanation screen. Provision identities using your product flow or another tool, then run this
sample again.

Requirements
************

- **QEMU (default):** ``qemu_x86`` board — display uses QEMU **ramfb** at **1024×768**. Use a QEMU
  UI backend (for example SDL or GTK) to see the framebuffer.
- **Hardware (optional):** UART-connected module supported by Zephyr’s biometrics drivers, with a
  devicetree alias ``biometrics`` pointing at the sensor node.

Building and Running (QEMU)
*****************************

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/biometrics/lvgl_identify
   :board: qemu_x86
   :goals: build run
   :compact:

Exit QEMU with :kbd:`CTRL+A` :kbd:`x`.

Using a DFRobot AI10 on UART (later)
************************************

Point the ``biometrics`` alias at a ``dfrobot,ai10`` child on the UART wired to the module
(**115200 8N1**). See :zephyr:code-sample:`dfrobot-ai10-face` for module behavior, UIDs after
enrollment, and a full overlay example.

On ``qemu_x86``, ``uart1`` is shared with other nodes in the default board DTS; on your real board,
enable the chosen UART, disable conflicting consumers if needed, and add a child node:

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

Adjust ``&uart1`` to match your SoC’s devicetree label. Face recognition may need longer match
timeouts than the emulated sensor; tune ``K_SECONDS`` in the source if required.
