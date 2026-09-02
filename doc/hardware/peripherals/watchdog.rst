.. _watchdog_api:

Watchdog
########

Overview
********

A watchdog timer is a hardware counter that triggers a recovery action, usually a reset of the
SoC, unless software restarts it before it expires, which is called feeding the watchdog. When the
application hangs and stops feeding it, the watchdog brings the system back to a known state.

The watchdog API provides a generic way to drive such peripherals, whether they are built into the
SoC, part of an external supervisor chip or PMIC, or emulated with a counter (see
:ref:`watchdog_software`). An application installs one or more timeouts, starts the watchdog, and
then feeds each timeout periodically. Key concepts include:

**Timeouts and channels**
  A timeout is described by a :c:struct:`wdt_timeout_cfg` structure and installed with
  :c:func:`wdt_install_timeout`, which returns the channel ID to pass to :c:func:`wdt_feed`.
  Hardware with several channels can supervise several activities with one watchdog instance.

**Feed window**
  The :c:struct:`wdt_window` of each timeout gives the earliest and latest moment, in
  milliseconds, at which a feed is accepted. Feeding outside this window triggers the watchdog.

**Reset flags**
  :c:member:`wdt_timeout_cfg.flags` selects what happens when a timeout expires:
  :c:macro:`WDT_FLAG_RESET_SOC`, :c:macro:`WDT_FLAG_RESET_CPU_CORE`, or
  :c:macro:`WDT_FLAG_RESET_NONE` when only the callback should run.

**Callback**
  An optional :c:type:`wdt_callback_t` function that the driver calls from its interrupt handler
  when a timeout expires, before the reset takes effect.

**Setup options**
  Instance-wide settings passed to :c:func:`wdt_setup`: :c:macro:`WDT_OPT_PAUSE_IN_SLEEP` and
  :c:macro:`WDT_OPT_PAUSE_HALTED_BY_DBG`.

Timeouts and Feed Windows
*************************

Each timeout is configured through a :c:struct:`wdt_timeout_cfg` structure:

* :c:member:`wdt_timeout_cfg.window` holds the feed window. :c:member:`wdt_window.max` is the
  timeout itself, and :c:member:`wdt_window.min` an opening time before which feeding is not
  allowed, a feature of window watchdogs such as the NXP WDOG32. Watchdogs without window support
  only accept a ``min`` of ``0``. Values that cannot be programmed exactly are rounded up.
* :c:member:`wdt_timeout_cfg.callback` is the function invoked when the timeout expires, or
  ``NULL``.
* :c:member:`wdt_timeout_cfg.flags` selects the action taken when the timeout expires. Support is
  hardware specific: the Nordic nRF driver accepts only :c:macro:`WDT_FLAG_RESET_SOC`, while the
  ESP32 and counter-based drivers also accept :c:macro:`WDT_FLAG_RESET_NONE`.
* :c:member:`wdt_timeout_cfg.next` chains staged timeouts and is ``NULL`` for the last or only
  stage. It only exists when :kconfig:option:`CONFIG_WDT_MULTISTAGE` is enabled, which requires a
  driver that selects :kconfig:option:`CONFIG_HAS_WDT_MULTISTAGE`; the OpenTitan AON timer driver,
  for example, expects a :c:macro:`WDT_FLAG_RESET_NONE` stage that only runs a callback, followed
  by a :c:macro:`WDT_FLAG_RESET_SOC` stage with a window at least as long.

:c:func:`wdt_install_timeout` validates the configuration against the hardware and returns a
non-negative channel ID on success, ``-EINVAL`` for a window outside the supported range
(including a ``max`` of ``0``), ``-ENOMEM`` when every channel is in use, and ``-ENOTSUP`` when a
flag or the callback is not supported. Watchdogs with several channels but a single shared timeout
value, such as the Nordic nRF WDT, also return ``-EINVAL`` when the windows of two timeouts differ.

Setup Options
*************

:c:func:`wdt_setup` starts the watchdog. It must be called after all timeouts have been installed:
from that point on every installed channel is armed and must be fed. Its ``options`` argument is a
bitmask of instance-wide settings: :c:macro:`WDT_OPT_PAUSE_IN_SLEEP` stops the counter while the
CPU is in a sleep state, so that a long stay in a low-power mode does not cause a reset, and
:c:macro:`WDT_OPT_PAUSE_HALTED_BY_DBG` stops it while the CPU is halted by a debugger, so that
stopping at a breakpoint does not cause one either. Passing ``0`` keeps the watchdog running in
every state. Support for each option is hardware specific: the STM32 IWDG, for example, rejects
:c:macro:`WDT_OPT_PAUSE_IN_SLEEP` because suspending it in low-power modes, on the SoCs that
support it at all, is selected through option bits rather than by software.
When an option is not supported, :c:func:`wdt_setup` returns ``-ENOTSUP`` and the caller can retry
with fewer options, as the driver tests do; ``-EBUSY`` means the watchdog is already running.

Devicetree Configuration
************************

Each watchdog instance is a devicetree node whose ``compatible`` property selects the driver, for
example :dtcompatible:`nordic,nrf-wdt`, :dtcompatible:`st,stm32-watchdog`,
:dtcompatible:`st,stm32-window-watchdog` or :dtcompatible:`espressif,esp32-watchdog`. The bindings
need little more than ``reg``, plus ``interrupts`` or ``clocks`` where the hardware requires them.
SoC devicetree files declare the nodes, usually with ``status = "disabled"``; the board or the
application enables the instance it wants to use and points the ``watchdog0`` alias at it, which
is how the :zephyr:code-sample:`watchdog` sample and, by default, the driver tests find their
device:

.. code-block:: devicetree
   :caption: Selecting the STM32 independent watchdog through the ``watchdog0`` alias

   / {
       aliases {
           watchdog0 = &iwdg;
       };
   };

   &iwdg {
       status = "okay";
   };

The :zephyr:board:`nucleo_f091rc` board devicetree contains exactly this configuration, and the
:zephyr_file:`samples/drivers/watchdog/boards/stm32_iwdg.overlay` overlay applies it to any STM32
board. Its companion :zephyr_file:`samples/drivers/watchdog/boards/stm32_wwdg.overlay` selects the
window watchdog instead by retargeting the alias, enabling ``wwdg`` and disabling ``iwdg``.

Basic Operation
***************

Typical use of the watchdog API is:

#. Get the watchdog device, usually with :c:macro:`DEVICE_DT_GET` on the ``watchdog0`` alias.
#. Fill a :c:struct:`wdt_timeout_cfg` structure and install it with :c:func:`wdt_install_timeout`,
   once per timeout, keeping the returned channel IDs.
#. Call :c:func:`wdt_setup` with the desired options. The watchdog is now running.
#. Feed every channel with :c:func:`wdt_feed` from the code path being supervised, always within
   the channel's window.
#. Optionally stop the watchdog with :c:func:`wdt_disable`, where the hardware allows it.

.. code-block:: c
   :caption: Installing a one second timeout, starting the watchdog and feeding it

   const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
   struct wdt_timeout_cfg cfg = {
       .window.min = 0U,
       .window.max = 1000U,
       .flags = WDT_FLAG_RESET_SOC,
   };
   int channel_id, err;

   if (!device_is_ready(wdt)) {
       return -ENODEV;
   }

   channel_id = wdt_install_timeout(wdt, &cfg);
   if (channel_id < 0) {
       return channel_id;
   }

   err = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
   if (err == -ENOTSUP) {
       /* Pausing under the debugger is not supported, run unconditionally instead */
       err = wdt_setup(wdt, 0);
   }
   if (err < 0) {
       return err;
   }

   while (1) {
       /* Do the supervised work, then feed well before the window closes */
       wdt_feed(wdt, channel_id);
       k_sleep(K_MSEC(500));
   }

Callbacks
=========

The driver invokes the :c:type:`wdt_callback_t` of an expired timeout from its interrupt handler,
passing the device and the channel ID, so the callback runs in interrupt context and must be
short. It typically records diagnostic information in memory that survives the reset or performs a
last emergency action. The time available is hardware specific: the :zephyr:code-sample:`watchdog`
sample disables its callback on Nordic nRF SoCs because the reset follows about 61 microseconds
after the interrupt. Combined with :c:macro:`WDT_FLAG_RESET_NONE`, the callback is the only
consequence of a missed feed, a mode exercised by the
:zephyr_file:`tests/drivers/watchdog/wdt_basic_reset_none` test.

Drivers that do not support callbacks select :kconfig:option:`CONFIG_HAS_WDT_NO_CALLBACKS` and
usually reject a non-``NULL`` callback with ``-ENOTSUP``; the STM32 IWDG driver, for instance,
accepts one only when :kconfig:option:`CONFIG_IWDG_STM32_EARLY_WAKEUP` is enabled. Portable code
handles this error by installing the timeout again without a callback, as the sample does.

Disabling and Reconfiguring
===========================

:c:func:`wdt_disable` stops the watchdog and uninstalls all of its timeouts; to run it again,
install new timeouts and call :c:func:`wdt_setup` once more. Since timeouts cannot be installed
while the watchdog is running, this is the only way to change a configuration. Whether it is
possible at all depends on the hardware: many watchdogs cannot be stopped once started, by design,
and their drivers return ``-EPERM``, the STM32 IWDG among them. Calling :c:func:`wdt_disable`
before the watchdog has been set up returns ``-EFAULT``.

Some watchdogs are enabled by hardware as soon as the SoC comes out of reset. Their drivers select
:kconfig:option:`CONFIG_HAS_WDT_DISABLE_AT_BOOT`, and :kconfig:option:`CONFIG_WDT_DISABLE_AT_BOOT`
then makes them turn the watchdog off during driver initialization, which leaves it unusable on
hardware that cannot re-enable it. Zephyr never starts a watchdog on its own: an application that
wants watchdog protection must install a timeout and call :c:func:`wdt_setup` itself.

Constraints and Error Handling
==============================

* Order matters: install timeouts, then set up, then feed. Installing after setup and setting up
  twice return ``-EBUSY``, feeding a channel that has not been installed returns ``-EINVAL``, and
  :c:func:`wdt_feed` returns ``-EAGAIN`` when it cannot complete without blocking, for example
  because the hardware is still synchronizing a previous feed, in which case the caller can retry.
  The :zephyr_file:`tests/drivers/watchdog/wdt_error_cases` test suite exercises these cases.
* :c:func:`wdt_setup`, :c:func:`wdt_disable` and :c:func:`wdt_feed` are system calls and can be
  used from user mode threads when :kconfig:option:`CONFIG_USERSPACE` is enabled (see
  :ref:`usermode_api`). :c:func:`wdt_install_timeout` is not a system call and must be called from
  a supervisor thread.
* The API has no power management hooks beyond :c:macro:`WDT_OPT_PAUSE_IN_SLEEP`: where that
  option is unsupported, the watchdog keeps counting while the CPU sleeps, so the application must
  wake up and feed it before the timeout elapses.
* After a watchdog reset, :c:func:`hwinfo_get_reset_cause` reports the :c:macro:`RESET_WATCHDOG`
  flag on devices that support it (see :ref:`hwinfo_api`).

.. _watchdog_software:

Software Watchdogs
******************

:kconfig:option:`CONFIG_WDT_COUNTER` builds a watchdog on top of a :ref:`counter <counter_api>`
device, described by a :dtcompatible:`zephyr,counter-watchdog` node whose ``counter`` property
points at the counter to use; :kconfig:option:`CONFIG_WDT_COUNTER_CH_COUNT` bounds its number of
channels. It supports :c:macro:`WDT_FLAG_RESET_NONE` and :c:macro:`WDT_FLAG_RESET_SOC`, rejects
both pause options, and calls its callback from the counter alarm handler before rebooting. It
complements a hardware watchdog rather than replacing it, since an interrupt cannot preempt code
of the same or higher priority, and mainly serves to get a callback with enough time to log debug
information on SoCs whose own watchdog resets too soon for that.

The :ref:`task watchdog <task_wdt_api>` is a software layer above this API that gives every
supervised thread its own channel even when the hardware has a single one. With
:kconfig:option:`CONFIG_TASK_WDT_HW_FALLBACK`, the hardware watchdog passed to
:c:func:`task_wdt_init` is set up with a single :c:macro:`WDT_FLAG_RESET_SOC` timeout and the
pause options selected by :kconfig:option:`CONFIG_TASK_WDT_HW_FALLBACK_PAUSE_HALTED_BY_DBG` and
:kconfig:option:`CONFIG_TASK_WDT_HW_FALLBACK_PAUSE_IN_SLEEP`, and is fed as long as every task
watchdog channel is fed in time.

Shell Commands
**************

When :kconfig:option:`CONFIG_WDT_SHELL` is enabled, ``wdt`` commands are available in the
:ref:`shell <shell_api>` to configure and feed a watchdog interactively. Each subcommand takes the
watchdog device name as its first argument, with tab completion. The following are available:

``wdt setup <device>``
  Start the watchdog with no options, as :c:func:`wdt_setup` called with ``0`` would.

``wdt disable <device>``
  Disable the watchdog and uninstall all of its timeouts.

``wdt timeout <device> <none|cpu|soc> <min_ms> <max_ms>``
  Install a timeout without a callback. The reset mode is given by name, or as the numeric value
  of the corresponding ``WDT_FLAG_RESET_*`` flag, and the window bounds are in milliseconds. The
  channel ID assigned to the timeout is printed on success.

``wdt feed <device> <channel_id>``
  Feed the given channel.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_WATCHDOG`
* :kconfig:option:`CONFIG_WDT_SHELL`
* :kconfig:option:`CONFIG_WDT_DISABLE_AT_BOOT`
* :kconfig:option:`CONFIG_WDT_MULTISTAGE`
* :kconfig:option:`CONFIG_WDT_COUNTER`
* :kconfig:option:`CONFIG_WDT_COUNTER_CH_COUNT`
* :kconfig:option:`CONFIG_TASK_WDT`

API Reference
*************

.. doxygengroup:: watchdog_interface
