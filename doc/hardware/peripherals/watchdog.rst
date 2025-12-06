.. _watchdog_api:

Watchdog (WDT)
##############

Overview
********

A watchdog timer (WDT) is a hardware timer that automatically resets the system if the software
fails to periodically service ("feed") it. This is a critical safety feature for embedded systems
that helps recover from software failures, deadlocks, or infinite loops.

The Watchdog API provides a generic method to configure and manage watchdog timers. It allows
applications to install timeouts, configure reset behavior, and periodically service the watchdog
to prevent system resets. Key features include:

**Timeout Configuration**
  Install one or more timeout channels with configurable time windows.
  Support for both simple timeouts and window timeouts (with minimum and maximum bounds).

**Reset Behavior**
  Configure the reset scope: no reset, CPU core reset, or full SoC reset.
  Optional callback support before reset occurs (hardware-dependent).

**Multistage Timeouts**
  Some watchdogs support staged timeout operations where multiple actions can be
  configured in sequence (requires :kconfig:option:`CONFIG_WDT_MULTISTAGE`).

**Flexible Options**
  Pause watchdog during sleep or when halted by debugger.
  Hardware-specific configuration options.

Devicetree Configuration
************************

Watchdog controllers are defined in the Devicetree with device-specific compatible strings.
The watchdog peripheral is typically pre-configured in the board's devicetree and referenced
through an alias.

Example of a watchdog controller definition:

.. code-block:: devicetree

   wdt0: watchdog@40010000 {
       compatible = "nordic,nrf-wdt";
       reg = <0x40010000 0x1000>;
       interrupts = <16 0>;
       status = "okay";
   };

   aliases {
       watchdog0 = &wdt0;
   };

Most watchdog implementations do not require additional properties in the devicetree beyond
the hardware configuration. The timeout windows and behavior are configured at runtime using
the API.

Basic Operation
***************

The typical workflow for using a watchdog timer is:

1. Get the watchdog device
2. Configure timeout(s) using :c:func:`wdt_install_timeout`
3. Set up the watchdog with :c:func:`wdt_setup`
4. Periodically feed the watchdog with :c:func:`wdt_feed`

Getting the Watchdog Device
============================

The watchdog device is typically obtained using the devicetree alias:

.. code-block:: c
   :caption: Getting the watchdog device using the ``watchdog0`` alias

   #include <zephyr/drivers/watchdog.h>

   const struct device *wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));

   if (!device_is_ready(wdt)) {
       printk("Watchdog device not ready\n");
       return -ENODEV;
   }

Installing a Timeout
====================

Before setting up the watchdog, you must install at least one timeout configuration.
This defines the timeout window and reset behavior:

.. code-block:: c
   :caption: Installing a basic watchdog timeout

   struct wdt_timeout_cfg wdt_config = {
       .flags = WDT_FLAG_RESET_SOC,  /* Reset SoC when watchdog timer expires */
       /* Expire watchdog after 1000ms */
       .window.min = 0U,
       .window.max = 1000U,
       .callback = NULL,  /* No callback */
   };

   int channel_id = wdt_install_timeout(wdt, &wdt_config);
   if (channel_id < 0) {
       printk("Failed to install watchdog timeout (err %d)\n", channel_id);
       return channel_id;
   }

The function returns a channel ID that must be used when feeding the watchdog.

Setting Up the Watchdog
========================

After installing timeout(s), call :c:func:`wdt_setup` to activate the watchdog:

.. code-block:: c
   :caption: Setting up the watchdog with pause-on-debug option

   int ret = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
   if (ret < 0) {
       printk("Watchdog setup error (err %d)\n", ret);
       return ret;
   }

   /* Watchdog is now active and must be fed periodically */

Available options:

* :c:macro:`WDT_OPT_PAUSE_IN_SLEEP` - Pause watchdog when CPU is in sleep state
* :c:macro:`WDT_OPT_PAUSE_HALTED_BY_DBG` - Pause watchdog when CPU is halted by debugger

Feeding the Watchdog
====================

Once the watchdog is active, it must be fed periodically before the timeout expires:

.. code-block:: c
   :caption: Feeding the watchdog periodically in a loop

   while (1) {
       /* Do application work */
       do_application_work();
       
       /* Feed the watchdog before timeout expires */
       ret = wdt_feed(wdt, channel_id);
       if (ret < 0) {
           printk("Watchdog feed error (err %d)\n", ret);
       }
       
       /* Sleep for a duration less than the watchdog timeout */
       k_msleep(500);
   }

Refer to :zephyr:code-sample:`watchdog` for a complete working example.

Window Watchdog
***************

Some watchdog implementations support window timeouts, where the watchdog must be fed
within a specific time window (between min and max values). Feeding too early (before min)
or too late (after max) will trigger the watchdog.

.. code-block:: c
   :caption: Configuring a window watchdog timeout

   struct wdt_timeout_cfg wdt_config = {
       .flags = WDT_FLAG_RESET_SOC,
       .window.min = 100U,  /* Must feed between 100ms and 1000ms */
       .window.max = 1000U,
       .callback = NULL,
   };

When using window timeouts:

* Wait at least ``min`` milliseconds before the first feed
* Feed before ``max`` milliseconds expire
* Continue feeding within the window on subsequent iterations

Watchdog Callbacks
******************

Some watchdog implementations support callbacks that are invoked when a timeout occurs,
before the system reset. This allows the application to perform last-moment actions such
as logging diagnostic information.

.. note::
   Not all hardware supports callbacks. Check your specific watchdog driver documentation.
   Some implementations have very short callback windows (microseconds) that limit what
   can be accomplished.

.. code-block:: c
   :caption: Using a watchdog callback

   static void wdt_callback(const struct device *dev, int channel_id)
   {
       /* This is called when timeout occurs, before reset */
       printk("Watchdog timeout on channel %d! System will reset.\n", channel_id);
       /* Can perform emergency actions here */
       /* Note: Time is limited, keep actions minimal */
   }

   struct wdt_timeout_cfg wdt_config = {
       .flags = WDT_FLAG_RESET_SOC,
       .window.min = 0U,
       .window.max = 1000U,
       .callback = wdt_callback,
   };

If a driver does not support callbacks with the ``WDT_FLAG_RESET_SOC`` flag,
:c:func:`wdt_install_timeout` will return ``-ENOTSUP``. You can retry without a callback
or use ``WDT_FLAG_RESET_NONE`` if supported.

Multistage Timeouts
*******************

When :kconfig:option:`CONFIG_WDT_MULTISTAGE` is enabled, some watchdog implementations
support staging multiple timeout actions. This allows you to configure a sequence of
escalating responses to a watchdog timeout.

.. code-block:: c
   :caption: Configuring multistage watchdog timeouts

   /* Second stage: Reset SoC */
   struct wdt_timeout_cfg stage2 = {
       .flags = WDT_FLAG_RESET_SOC,
       .window.min = 0U,
       .window.max = 2000U,
       .callback = NULL,
       .next = NULL,
   };

   /* First stage: Trigger callback */
   struct wdt_timeout_cfg stage1 = {
       .flags = WDT_FLAG_RESET_NONE,
       .window.min = 0U,
       .window.max = 1000U,
       .callback = wdt_callback,
       .next = &stage2,  /* Chain to next stage */
   };

   int channel_id = wdt_install_timeout(wdt, &stage1);

In this example:
1. First timeout at 1000ms triggers a callback with no reset
2. If not serviced, second timeout at 2000ms resets the SoC

Disabling the Watchdog
***********************

Some watchdog implementations allow disabling after they have been set up:

.. code-block:: c
   :caption: Disabling an active watchdog

   int ret = wdt_disable(wdt);
   if (ret == -EPERM) {
       printk("This watchdog cannot be disabled\n");
   } else if (ret < 0) {
       printk("Failed to disable watchdog (err %d)\n", ret);
   }

.. warning::
   Many watchdog implementations do not support disabling once enabled, as this would
   defeat the purpose of the watchdog as a safety mechanism. The :c:func:`wdt_disable`
   function may return ``-EPERM`` on such hardware.

Reset Behavior Flags
********************

The watchdog timeout configuration supports different reset behaviors:

* :c:macro:`WDT_FLAG_RESET_NONE` - No reset, callback only (if supported)
* :c:macro:`WDT_FLAG_RESET_CPU_CORE` - Reset only the CPU core
* :c:macro:`WDT_FLAG_RESET_SOC` - Reset the entire SoC (most common)

The availability of these flags depends on the hardware capabilities. Check the specific
driver documentation for your platform.

Configuration Options
*********************

Main configuration options:

* :kconfig:option:`CONFIG_WATCHDOG` - Enable watchdog driver support
* :kconfig:option:`CONFIG_WDT_MULTISTAGE` - Enable multistage timeout support
* :kconfig:option:`CONFIG_WDT_DISABLE_AT_BOOT` - Disable watchdog at boot (hardware-dependent)
* :kconfig:option:`CONFIG_WDT_COUNTER` - Enable counter-based software watchdog

Driver-specific options are available in the Kconfig files under ``drivers/watchdog/``.

API Reference
*************

.. doxygengroup:: watchdog_interface
