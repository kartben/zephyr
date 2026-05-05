.. _input_api:

Input
#####

Overview
********

The input subsystem provides a uniform interface for input devices such as
keyboards, buttons, touchscreens, mice, trackpads, joysticks, and rotary
encoders. It is modelled after the Linux input layer, using an event-based
approach where each physical action generates one or more :c:struct:`input_event`
structures.

Each event carries:

- The originating device pointer (or ``NULL`` for anonymous events).
- An event type (e.g., :c:macro:`INPUT_EV_KEY`, :c:macro:`INPUT_EV_ABS`,
  :c:macro:`INPUT_EV_REL`).
- An event code that identifies the specific key, button, or axis.
- A signed 32-bit value (e.g., 1/0 for key press/release, or a coordinate).
- A *sync* flag that marks the last event in a logical group of simultaneous
  events (e.g., an X/Y touch coordinate pair).

Receiving events
================

Applications and subsystems receive input events by registering a callback
with the :c:macro:`INPUT_CALLBACK_DEFINE` macro. The callback is called in the
context of the input thread (or in ISR context when
:kconfig:option:`CONFIG_INPUT_MODE_SYNCHRONOUS` is enabled).

.. code-block:: c

   static void my_input_cb(struct input_event *evt, void *user_data)
   {
       if (evt->type == INPUT_EV_KEY && evt->code == INPUT_KEY_ENTER) {
           if (evt->value) {
               printk("Enter pressed\n");
           }
       }
   }

   INPUT_CALLBACK_DEFINE(NULL, my_input_cb, NULL);

Passing ``NULL`` as the first argument subscribes the callback to events from
all input devices. Pass a specific device pointer to receive events from that
device only.

Reporting events
================

Driver authors report new events with :c:func:`input_report` (or the type-
specific helpers :c:func:`input_report_key`, :c:func:`input_report_abs`, and
:c:func:`input_report_rel`). The final event in a multi-event group should
set the *sync* flag to ``true``.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_INPUT`
* :kconfig:option:`CONFIG_INPUT_MODE_SYNCHRONOUS`
* :kconfig:option:`CONFIG_INPUT_MODE_THREAD`
* :kconfig:option:`CONFIG_INPUT_THREAD_STACK_SIZE`

API Reference
*************

.. doxygengroup:: input_interface
