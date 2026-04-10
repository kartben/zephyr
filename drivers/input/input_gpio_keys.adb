--  Copyright (c) 2024 Google LLC
--  SPDX-License-Identifier: Apache-2.0
--
--  Ada implementation of the GPIO Keys input driver.
--
--  All Zephyr C API calls and struct-field accesses are performed through thin
--  C helper functions ("accessors") declared in input_gpio_keys.c.  This
--  keeps the Ada code architecture-independent: no struct-layout knowledge or
--  C macro magic lives here.

with Interfaces.C; use Interfaces.C;
with System;

package body Input_GPIO_Keys is

   --------------------------------------------------------------------------
   --  C accessor imports
   --  These are thin wrappers defined in input_gpio_keys.c that expose
   --  the gpio_keys_* struct fields and relevant Zephyr APIs to Ada.
   --------------------------------------------------------------------------

   --  const struct gpio_keys_config *gpio_keys_acc_get_config(const struct device *)
   function Acc_Get_Config (Dev : System.Address) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_get_config";

   --  struct gpio_keys_data *gpio_keys_acc_get_data(const struct device *)
   function Acc_Get_Data (Dev : System.Address) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_get_data";

   --  int gpio_keys_acc_num_keys(const void *cfg)
   function Acc_Num_Keys (Cfg : System.Address) return int
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_num_keys";

   --  const void *gpio_keys_acc_pin_cfg(const void *cfg, int i)
   function Acc_Pin_Cfg (Cfg : System.Address; I : int) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_pin_cfg";

   --  void *gpio_keys_acc_pin_data(const void *cfg, int i)
   function Acc_Pin_Data (Cfg : System.Address; I : int) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_pin_data";

   --  uint32_t gpio_keys_acc_debounce_ms(const void *cfg)
   function Acc_Debounce_Ms (Cfg : System.Address) return unsigned
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_debounce_ms";

   --  int gpio_keys_acc_polling_mode(const void *cfg)  -- bool
   function Acc_Polling_Mode (Cfg : System.Address) return int
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_polling_mode";

   --  int gpio_keys_acc_no_disconnect(const void *cfg)  -- bool
   function Acc_No_Disconnect (Cfg : System.Address) return int
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_no_disconnect";

   --  const void *gpio_keys_acc_pin_cfg_spec(const void *pin_cfg)
   --  Returns &pin_cfg->spec (a const struct gpio_dt_spec *)
   function Acc_Pin_Cfg_Spec (Pin_Cfg : System.Address) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_pin_cfg_spec";

   --  uint32_t gpio_keys_acc_pin_cfg_code(const void *pin_cfg)
   function Acc_Pin_Cfg_Code (Pin_Cfg : System.Address) return unsigned
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_pin_cfg_code";

   --  const void *gpio_keys_acc_spec_port_name(const void *spec)
   --  Returns spec->port->name (const char *), for logging
   function Acc_Spec_Port_Name (Spec : System.Address) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_spec_port_name";

   --  const struct device *gpio_keys_acc_pin_data_dev(const void *pin_data)
   function Acc_Pin_Data_Dev (Pin_Data : System.Address) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_pin_data_dev";

   --  void gpio_keys_acc_pin_data_set_dev(void *pin_data, const struct device *)
   procedure Acc_Pin_Data_Set_Dev
     (Pin_Data : System.Address; Dev : System.Address)
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_pin_data_set_dev";

   --  int gpio_keys_acc_pin_state(const void *pin_data)
   --  Returns pin_data->cb_data.pin_state
   function Acc_Pin_State (Pin_Data : System.Address) return int
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_pin_state";

   --  void gpio_keys_acc_set_pin_state(void *pin_data, int state)
   procedure Acc_Set_Pin_State (Pin_Data : System.Address; State : int)
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_set_pin_state";

   --  void *gpio_keys_acc_pin_data_cb(void *pin_data)
   --  Returns &pin_data->cb_data (struct gpio_keys_callback *)
   function Acc_Pin_Data_CB (Pin_Data : System.Address) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_pin_data_cb";

   --  void *gpio_keys_acc_pin_data_work(void *pin_data)
   --  Returns &pin_data->work (struct k_work_delayable *)
   function Acc_Pin_Data_Work (Pin_Data : System.Address) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_pin_data_work";

   --  void *gpio_keys_acc_work_to_pin_data(struct k_work *)
   --  Equivalent of CONTAINER_OF(dwork, gpio_keys_pin_data, work)
   function Acc_Work_To_Pin_Data (Work : System.Address) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_work_to_pin_data";

   --  void *gpio_keys_acc_keys_cb_from_cb(struct gpio_callback *)
   --  Equivalent of CONTAINER_OF(cbdata, gpio_keys_callback, gpio_cb)
   function Acc_Keys_CB_From_CB (CB : System.Address) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_keys_cb_from_cb";

   --  void *gpio_keys_acc_cb_to_pin_data(struct gpio_keys_callback *)
   --  Equivalent of CONTAINER_OF(keys_cb, gpio_keys_pin_data, cb_data)
   function Acc_CB_To_Pin_Data (Keys_CB : System.Address) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_cb_to_pin_data";

   --  int gpio_keys_acc_pin_data_index(const void *cfg, const void *pin_data)
   --  Returns (pin_data - cfg->pin_data) as an element index
   function Acc_Pin_Data_Index
     (Cfg : System.Address; Pin_Data : System.Address) return int
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_pin_data_index";

   --  int gpio_keys_acc_is_suspended(const void *data)
   --  Returns atomic_get(&data->suspended)
   function Acc_Is_Suspended (Data : System.Address) return int
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_is_suspended";

   --  void gpio_keys_acc_set_suspended(void *data, int val)
   procedure Acc_Set_Suspended (Data : System.Address; Val : int)
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_set_suspended";

   --  void gpio_keys_acc_work_init(void *pin_data, int is_polling)
   --  Calls k_work_init_delayable with the appropriate handler
   procedure Acc_Work_Init (Pin_Data : System.Address; Is_Polling : int)
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_work_init";

   --  void gpio_keys_acc_work_reschedule(struct k_work_delayable *, uint32_t ms)
   --  Calls k_work_reschedule(dwork, K_MSEC(ms))
   procedure Acc_Work_Reschedule (Work : System.Address; Ms : unsigned)
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_work_reschedule";

   --  int gpio_keys_acc_configure_interrupt(spec, cb, pin_data)
   --  Calls gpio_init_callback + gpio_add_callback + reads initial state
   --  + gpio_pin_interrupt_configure_dt(GPIO_INT_EDGE_BOTH).
   --  Returns 0 on success, negative errno on failure.
   function Acc_Configure_Interrupt
     (Spec     : System.Address;
      CB       : System.Address;
      Pin_Data : System.Address) return int
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_configure_interrupt";

   --  void gpio_keys_acc_report_key(dev, code, pressed)
   --  Calls input_report_key(dev, code, pressed, true, K_FOREVER)
   procedure Acc_Report_Key
     (Dev     : System.Address;
      Code    : unsigned;
      Pressed : int)
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_report_key";

   --  const char *gpio_keys_acc_dev_name(const struct device *)
   function Acc_Dev_Name (Dev : System.Address) return System.Address
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_dev_name";

   --  GPIO pin operations (wrap gpio_pin_*_dt with the right flags)
   --  int gpio_pin_get_dt(const struct gpio_dt_spec *)
   function GPIO_Pin_Get_DT (Spec : System.Address) return int
     with Import, Convention => C, External_Name => "gpio_pin_get_dt";

   --  bool gpio_is_ready_dt(const struct gpio_dt_spec *)
   function GPIO_Is_Ready_DT (Spec : System.Address) return int
     with Import, Convention => C, External_Name => "gpio_is_ready_dt";

   --  int gpio_keys_acc_gpio_configure_input(spec)
   function Acc_GPIO_Configure_Input (Spec : System.Address) return int
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_gpio_configure_input";

   --  int gpio_keys_acc_gpio_configure_disconnected(spec)
   function Acc_GPIO_Configure_Disconnected
     (Spec : System.Address) return int
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_gpio_configure_disconnected";

   --  int gpio_keys_acc_gpio_interrupt_disable(spec)
   function Acc_GPIO_Interrupt_Disable (Spec : System.Address) return int
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_gpio_interrupt_disable";

   --  int gpio_keys_acc_gpio_interrupt_enable_both(spec)
   function Acc_GPIO_Interrupt_Enable_Both (Spec : System.Address) return int
     with Import, Convention => C,
          External_Name => "gpio_keys_acc_gpio_interrupt_enable_both";

   --  int pm_device_runtime_enable(const struct device *)
   function PM_Device_Runtime_Enable (Dev : System.Address) return int
     with Import, Convention => C,
          External_Name => "pm_device_runtime_enable";

   --------------------------------------------------------------------------
   --  Logging helpers (LOG_* are C macros; call thin C shims)
   --------------------------------------------------------------------------

   procedure Log_Err_Key_Get_Failed (Key_Index : int; Ret : int)
     with Import, Convention => C,
          External_Name => "gpio_keys_log_err_key_get_failed";

   procedure Log_Dbg_Poll
     (Name        : System.Address;
      Pin_State   : int;
      New_Pressed : int;
      Key_Index   : int)
     with Import, Convention => C,
          External_Name => "gpio_keys_log_dbg_poll";

   procedure Log_Dbg_Report
     (Name        : System.Address;
      New_Pressed : int;
      Code        : unsigned)
     with Import, Convention => C,
          External_Name => "gpio_keys_log_dbg_report";

   procedure Log_Err_Not_Ready (Port_Name : System.Address)
     with Import, Convention => C,
          External_Name => "gpio_keys_log_err_not_ready";

   procedure Log_Err_Pin_Config_Failed (I : int; Ret : int)
     with Import, Convention => C,
          External_Name => "gpio_keys_log_err_pin_config_failed";

   procedure Log_Err_Interrupt_Config_Failed (I : int; Ret : int)
     with Import, Convention => C,
          External_Name => "gpio_keys_log_err_interrupt_config_failed";

   procedure Log_Err_PM_Failed (Ret : int)
     with Import, Convention => C,
          External_Name => "gpio_keys_log_err_pm_failed";

   procedure Log_Err_Interrupt_Configure_Failed (Ret : int)
     with Import, Convention => C,
          External_Name => "gpio_keys_log_err_interrupt_configure_failed";

   --------------------------------------------------------------------------
   --  Negative errno constants (Zephyr: ENODEV=19, ENOTSUP=134)
   --------------------------------------------------------------------------

   ENODEV  : constant int := -19;
   ENOTSUP : constant int := -134;

   --------------------------------------------------------------------------
   --  PM action enum values  (enum pm_device_action, 0-based)
   --------------------------------------------------------------------------

   PM_DEVICE_ACTION_SUSPEND : constant int := 0;
   PM_DEVICE_ACTION_RESUME  : constant int := 1;

   --------------------------------------------------------------------------
   --  Internal: read a single pin and report any state change
   --------------------------------------------------------------------------

   procedure Poll_Pin (Dev : System.Address; Key_Index : int) is
      Cfg         : constant System.Address := Acc_Get_Config (Dev);
      Pin_Cfg     : constant System.Address := Acc_Pin_Cfg (Cfg, Key_Index);
      Pin_Data    : constant System.Address := Acc_Pin_Data (Cfg, Key_Index);
      Spec        : constant System.Address := Acc_Pin_Cfg_Spec (Pin_Cfg);
      Ret         : int;
      New_Pressed : int;
   begin
      Ret := GPIO_Pin_Get_DT (Spec);
      if Ret < 0 then
         Log_Err_Key_Get_Failed (Key_Index, Ret);
         return;
      end if;

      New_Pressed := Ret;

      Log_Dbg_Poll (Acc_Dev_Name (Dev),
                    Acc_Pin_State (Pin_Data),
                    New_Pressed,
                    Key_Index);

      if New_Pressed /= Acc_Pin_State (Pin_Data) then
         Acc_Set_Pin_State (Pin_Data, New_Pressed);
         Log_Dbg_Report (Acc_Dev_Name (Dev),
                         New_Pressed,
                         Acc_Pin_Cfg_Code (Pin_Cfg));
         Acc_Report_Key (Dev, Acc_Pin_Cfg_Code (Pin_Cfg), New_Pressed);
      end if;
   end Poll_Pin;

   --------------------------------------------------------------------------
   --  Exported: polling-mode work handler  (polls all pins periodically)
   --------------------------------------------------------------------------

   procedure Poll_Pins (Work : System.Address) is
      Pin_Data : constant System.Address := Acc_Work_To_Pin_Data (Work);
      Dev      : constant System.Address := Acc_Pin_Data_Dev (Pin_Data);
      Cfg      : constant System.Address := Acc_Get_Config (Dev);
      Num_Keys : constant int            := Acc_Num_Keys (Cfg);
   begin
      if Acc_Is_Suspended (Acc_Get_Data (Dev)) = 1 then
         return;
      end if;

      for I in 0 .. Num_Keys - 1 loop
         Poll_Pin (Dev, I);
      end loop;

      Acc_Work_Reschedule (Work, Acc_Debounce_Ms (Cfg));
   end Poll_Pins;

   --------------------------------------------------------------------------
   --  Exported: interrupt-driven debounce work handler (single pin)
   --------------------------------------------------------------------------

   procedure Change_Deferred (Work : System.Address) is
      Pin_Data  : constant System.Address := Acc_Work_To_Pin_Data (Work);
      Dev       : constant System.Address := Acc_Pin_Data_Dev (Pin_Data);
      Cfg       : constant System.Address := Acc_Get_Config (Dev);
      Key_Index : constant int            :=
        Acc_Pin_Data_Index (Cfg, Pin_Data);
   begin
      if Acc_Is_Suspended (Acc_Get_Data (Dev)) = 1 then
         return;
      end if;

      Poll_Pin (Dev, Key_Index);
   end Change_Deferred;

   --------------------------------------------------------------------------
   --  Exported: GPIO interrupt callback
   --  Schedules the per-pin debounce work item.
   --------------------------------------------------------------------------

   procedure Interrupt
     (Port    : System.Address;
      CB_Data : System.Address;
      Pins    : unsigned)
   is
      pragma Unreferenced (Port, Pins);
      Keys_CB  : constant System.Address := Acc_Keys_CB_From_CB (CB_Data);
      Pin_Data : constant System.Address := Acc_CB_To_Pin_Data (Keys_CB);
      Cfg      : constant System.Address :=
        Acc_Get_Config (Acc_Pin_Data_Dev (Pin_Data));
   begin
      Acc_Work_Reschedule (Acc_Pin_Data_Work (Pin_Data),
                           Acc_Debounce_Ms (Cfg));
   end Interrupt;

   --------------------------------------------------------------------------
   --  Exported: driver initialisation (POST_KERNEL)
   --------------------------------------------------------------------------

   function Init (Dev : System.Address) return int is
      Cfg      : constant System.Address := Acc_Get_Config (Dev);
      Num_Keys : constant int            := Acc_Num_Keys (Cfg);
      Ret      : int;
   begin
      for I in 0 .. Num_Keys - 1 loop
         declare
            Pin_Cfg  : constant System.Address := Acc_Pin_Cfg (Cfg, I);
            Pin_Data : constant System.Address := Acc_Pin_Data (Cfg, I);
            Spec     : constant System.Address := Acc_Pin_Cfg_Spec (Pin_Cfg);
         begin
            if GPIO_Is_Ready_DT (Spec) = 0 then
               Log_Err_Not_Ready (Acc_Spec_Port_Name (Spec));
               return ENODEV;
            end if;

            Ret := Acc_GPIO_Configure_Input (Spec);
            if Ret /= 0 then
               Log_Err_Pin_Config_Failed (I, Ret);
               return Ret;
            end if;

            Acc_Pin_Data_Set_Dev (Pin_Data, Dev);
            Acc_Work_Init (Pin_Data, Acc_Polling_Mode (Cfg));

            if Acc_Polling_Mode (Cfg) = 0 then
               Ret := Acc_Configure_Interrupt
                 (Spec, Acc_Pin_Data_CB (Pin_Data), Pin_Data);
               if Ret /= 0 then
                  Log_Err_Interrupt_Config_Failed (I, Ret);
                  return Ret;
               end if;
            end if;
         end;
      end loop;

      if Acc_Polling_Mode (Cfg) /= 0 then
         --  Kick off the periodic polling work on pin 0's work item
         Acc_Work_Reschedule
           (Acc_Pin_Data_Work (Acc_Pin_Data (Cfg, 0)),
            Acc_Debounce_Ms (Cfg));
      end if;

      Ret := PM_Device_Runtime_Enable (Dev);
      if Ret < 0 then
         Log_Err_PM_Failed (Ret);
         return Ret;
      end if;

      return 0;
   end Init;

   --------------------------------------------------------------------------
   --  Exported: PM action handler
   --------------------------------------------------------------------------

   function PM_Action (Dev : System.Address; Action : int) return int is
      Cfg      : constant System.Address := Acc_Get_Config (Dev);
      Data     : constant System.Address := Acc_Get_Data (Dev);
      Num_Keys : constant int            := Acc_Num_Keys (Cfg);
      Ret      : int;
   begin
      if Action = PM_DEVICE_ACTION_SUSPEND then
         Acc_Set_Suspended (Data, 1);

         for I in 0 .. Num_Keys - 1 loop
            declare
               Spec : constant System.Address :=
                 Acc_Pin_Cfg_Spec (Acc_Pin_Cfg (Cfg, I));
            begin
               if Acc_Polling_Mode (Cfg) = 0 then
                  Ret := Acc_GPIO_Interrupt_Disable (Spec);
                  if Ret < 0 then
                     Log_Err_Interrupt_Configure_Failed (Ret);
                     return Ret;
                  end if;
               end if;

               if Acc_No_Disconnect (Cfg) = 0 then
                  Ret := Acc_GPIO_Configure_Disconnected (Spec);
                  if Ret /= 0 then
                     Log_Err_Pin_Config_Failed (I, Ret);
                     return Ret;
                  end if;
               end if;
            end;
         end loop;

         return 0;

      elsif Action = PM_DEVICE_ACTION_RESUME then
         Acc_Set_Suspended (Data, 0);

         for I in 0 .. Num_Keys - 1 loop
            declare
               Pin_Data : constant System.Address := Acc_Pin_Data (Cfg, I);
               Spec     : constant System.Address :=
                 Acc_Pin_Cfg_Spec (Acc_Pin_Cfg (Cfg, I));
            begin
               if Acc_No_Disconnect (Cfg) = 0 then
                  Ret := Acc_GPIO_Configure_Input (Spec);
                  if Ret /= 0 then
                     Log_Err_Pin_Config_Failed (I, Ret);
                     return Ret;
                  end if;
               end if;

               if Acc_Polling_Mode (Cfg) /= 0 then
                  --  Reschedule only once, using pin 0's work item
                  if I = 0 then
                     Acc_Work_Reschedule
                       (Acc_Pin_Data_Work (Pin_Data),
                        Acc_Debounce_Ms (Cfg));
                  end if;
               else
                  Ret := GPIO_Pin_Get_DT (Spec);
                  if Ret >= 0 then
                     Acc_Set_Pin_State (Pin_Data, Ret);
                  end if;

                  Ret := Acc_GPIO_Interrupt_Enable_Both (Spec);
                  if Ret < 0 then
                     Log_Err_Interrupt_Configure_Failed (Ret);
                     return Ret;
                  end if;
               end if;
            end;
         end loop;

         return 0;

      else
         return ENOTSUP;
      end if;
   end PM_Action;

end Input_GPIO_Keys;
