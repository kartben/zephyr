.. zephyr:code-sample:: agent_pad
   :name: Agent Pad
   :relevant-api: usbd_api usbd_hid_device input_interface led_strip_interface

   Turn the Adafruit MacroPad RP2040 into a control surface for AI coding agents.

Overview
********

This sample turns the :zephyr:board:`adafruit_macropad_rp2040` into a dedicated
control surface for AI coding agents, inspired by devices such as the
Work Louder x OpenAI *Codex Micro*. It ships with an integration for
`Claude Code <https://code.claude.com>`_, but the device side is agent agnostic.

The pad enumerates as a USB composite device:

* a **HID keyboard** used to send shortcuts and macros to the focused terminal,
* a **CDC-ACM serial port** used by the host to feed live agent status to the pad.

Features
========

**Agent keys** — the top six keys represent up to six concurrent agent sessions.
Each key's RGB LED shows the live status of its session:

========  ==============  =========================
Status    Color           Meaning
========  ==============  =========================
idle      dim white       session registered
thinking  breathing blue  agent is working
done      green           agent finished its turn
input     pulsing amber   agent needs your input
error     blinking red    something went wrong
========  ==============  =========================

Pressing an agent key acknowledges its status (returning it to idle) and
reports a ``key <n>`` event on the serial port so host-side tooling can react,
for example by focusing that session's terminal. When no session is registered,
the agent keys play a gentle idle animation.

**Macro keys** — the bottom six keys send actions from the active layer as HID
keystrokes. Two layers are provided:

* ``CLAUDE``: interrupt (:kbd:`Esc`), cycle permission mode (:kbd:`Shift+Tab`),
  verbose output (:kbd:`Ctrl+R`), prompt history (:kbd:`Up`), accept
  (:kbd:`Enter`), and a macro typing ``claude`` to start a session.
* ``SLASH``: common slash commands (``/compact``, ``/clear``, ``/model``,
  ``/resume``, ``/review``, ``/help``).

**Rotary encoder** — rotate to scroll (:kbd:`Up`/:kbd:`Down`), short press for
:kbd:`Enter`, long press to switch layers. The bottom row LEDs are tinted with
the active layer's color.

**OLED display** — an LVGL GUI shows the active layer, a tile per agent slot
(spinner while thinking, inverted when attention is needed) and a scrolling
ticker with the last event.

**Speaker** — plays short chimes when an agent finishes, needs input, or hits
an error, so you notice even when the pad is out of sight.

Serial protocol
***************

Line oriented, newline terminated, over the CDC-ACM port:

.. code-block:: none

   agent <slot> <idle|thinking|busy|done|input|error> [label]
   clear <slot|all>
   ping

Slots are numbered 1-6. The pad answers ``ping`` with ``pong`` and emits
``key <slot>`` when an agent key is pressed. For example:

.. code-block:: console

   $ echo "agent 2 thinking zephyr" > /dev/ttyACM0

Claude Code integration
***********************

:zephyr_file:`scripts/claude-pad.sh <samples/boards/adafruit/macropad_rp2040/agent_pad/scripts/claude-pad.sh>`
bridges `Claude Code hooks
<https://code.claude.com/docs/en/hooks>`_ to the pad. It assigns each Claude
Code session a slot and forwards status transitions. Copy it somewhere on your
``PATH`` and register it in your ``~/.claude/settings.json``:

.. code-block:: json

   {
     "hooks": {
       "SessionStart": [{"hooks": [{"type": "command", "command": "claude-pad.sh idle"}]}],
       "UserPromptSubmit": [{"hooks": [{"type": "command", "command": "claude-pad.sh thinking"}]}],
       "Notification": [{"hooks": [{"type": "command", "command": "claude-pad.sh input"}]}],
       "Stop": [{"hooks": [{"type": "command", "command": "claude-pad.sh done"}]}],
       "SessionEnd": [{"hooks": [{"type": "command", "command": "claude-pad.sh clear"}]}]
     }
   }

The script autodetects the pad's serial port; set ``CLAUDE_PAD_PORT`` to
override.

Building and flashing
*********************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/adafruit/macropad_rp2040/agent_pad
   :board: adafruit_macropad_rp2040
   :goals: build flash
   :flash-args: --runner uf2
   :compact:

Enter the UF2 bootloader by holding down the rotary encoder button (BOOT)
while pressing and releasing the reset button, then flash (or copy
``build/zephyr/zephyr.uf2`` to the ``RPI-RP2`` drive).
