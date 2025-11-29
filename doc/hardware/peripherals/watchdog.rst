.. _watchdog_api:

Watchdog
########

Overview
********

A watchdog timer (WDT) is a hardware timer that automatically generates a system reset or
triggers a callback if the system fails to feed (reset) it within a specified time period.
Watchdogs are primarily used to recover from software failures by detecting and recovering
from system malfunctions.

The Watchdog API provides a generic interface for watchdog timer peripherals. Key features include:

**Timeout Configuration**
  Configure timeout windows that specify when the watchdog must be fed.
  Support for minimum and maximum timeout boundaries (window watchdog).

**Feed Operation**
  Periodically feed (reset) the watchdog to prevent timeout.
  Multiple independent channels can be configured on supported hardware.

**Reset Behavior**
  Configure reset scope (CPU core, SoC, or none).
  Optional callbacks before reset occurs for cleanup operations.

**Operational Modes**
  Configure watchdog behavior during sleep and debug states.

Devicetree Configuration
************************

Watchdog devices are defined in the Devicetree with a watchdog-specific compatible property.
The exact compatible string depends on the hardware vendor.

Example of a watchdog device definition:

.. code-block:: devicetree

   wdt0: watchdog@40010000 {
       compatible = "st,stm32-window-watchdog";
       reg = <0x40010000 0x400>;
       interrupts = <0 7>;
       status = "okay";
   };

Example of referencing a watchdog as an alias:

.. code-block:: devicetree

   aliases {
       watchdog0 = &wdt0;
   };

Basic Operation
***************

Watchdog operations typically involve installing a timeout configuration and
periodically feeding the watchdog to prevent system reset.

.. code-block:: c
   :caption: Configure and use a watchdog timer

   #include <zephyr/drivers/watchdog.h>

   static void wdt_callback(const struct device *wdt_dev, uint8_t channel_id)
   {
       printk("Watchdog callback - preparing for reset\n");
       /* Perform any necessary cleanup before reset */
   }

   void setup_watchdog(void)
   {
       const struct device *wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
       struct wdt_timeout_cfg wdt_config;
       int wdt_channel_id;
       int err;

       if (!device_is_ready(wdt)) {
           printk("Watchdog device not ready\n");
           return;
       }

       /* Configure watchdog */
       wdt_config.flags = WDT_FLAG_RESET_SOC;
       wdt_config.window.min = 0U;
       wdt_config.window.max = 1000U; /* 1 second timeout */
       wdt_config.callback = wdt_callback;

       /* Install timeout */
       wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
       if (wdt_channel_id < 0) {
           printk("Failed to install watchdog timeout\n");
           return;
       }

       /* Start the watchdog */
       err = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
       if (err < 0) {
           printk("Failed to setup watchdog\n");
           return;
       }

       /* Feed the watchdog periodically */
       while (1) {
           wdt_feed(wdt, wdt_channel_id);
           k_sleep(K_MSEC(500));
       }
   }

Refer to :zephyr:code-sample:`watchdog` for a complete example of using the Watchdog API.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_WATCHDOG`
* :kconfig:option:`CONFIG_WDT_MULTISTAGE`
* :kconfig:option:`CONFIG_WDT_DISABLE_AT_BOOT`

API Reference
*************

.. doxygengroup:: watchdog_interface
