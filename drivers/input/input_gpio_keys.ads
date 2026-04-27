--  Copyright (c) 2024 Google LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Ada specification for the GPIO Keys input driver.
--  This package declares the driver entry-points that are exported to C so
--  that the devicetree-generated registration code (which must live in C) can
--  reference them through the normal Zephyr device-driver API.

with Interfaces.C;
with System;

package Input_GPIO_Keys is

   pragma Preelaborate;

   --  Called once per device instance during POST_KERNEL initialisation.
   --  Signature matches Zephyr's device_init_t (int (*)(const struct device *)).
   function Init (Dev : System.Address) return Interfaces.C.int
     with Export        => True,
          Convention    => C,
          External_Name => "gpio_keys_init";

   --  Work-queue handler used in interrupt-driven (debounce) mode.
   --  Signature matches k_work_handler_t (void (*)(struct k_work *)).
   procedure Change_Deferred (Work : System.Address)
     with Export        => True,
          Convention    => C,
          External_Name => "gpio_keys_change_deferred";

   --  Work-queue handler used in polling mode.
   --  Same k_work_handler_t signature.
   procedure Poll_Pins (Work : System.Address)
     with Export        => True,
          Convention    => C,
          External_Name => "gpio_keys_poll_pins";

   --  GPIO interrupt callback.
   --  Matches gpio_callback_handler_t:
   --    void (*)(const struct device *, struct gpio_callback *, gpio_port_pins_t)
   procedure Interrupt
     (Port    : System.Address;
      CB_Data : System.Address;
      Pins    : Interfaces.C.unsigned)
     with Export        => True,
          Convention    => C,
          External_Name => "gpio_keys_interrupt";

   --  PM action callback (always exported; called only when CONFIG_PM_DEVICE=y).
   --  Matches pm_device_action_cb_t:
   --    int (*)(const struct device *, enum pm_device_action)
   function PM_Action
     (Dev    : System.Address;
      Action : Interfaces.C.int) return Interfaces.C.int
     with Export        => True,
          Convention    => C,
          External_Name => "gpio_keys_pm_action";

end Input_GPIO_Keys;
