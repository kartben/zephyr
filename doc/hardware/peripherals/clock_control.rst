.. _clock_control_api:

Clock Control
#############

Overview
********

A clock controller generates and distributes the clock signals of an SoC: oscillators, PLLs,
dividers, multiplexers and the gates that feed individual peripherals. A peripheral's registers
are usually only accessible while its bus clock is enabled, and gating unused clocks saves power.

The clock control API gives drivers and applications a uniform way to turn such clocks on and off,
check whether a clock is running, read or change its frequency, and apply controller-specific
settings, without knowing the register layout of the controller. Which clocks exist, and how they
are identified, is defined by each driver and normally described in devicetree. Key concepts are:

**Clock controller**
  A device whose driver implements :c:struct:`clock_control_driver_api`. Every operation in that
  structure is optional: calling one the driver does not provide returns ``-ENOSYS``, and
  :c:func:`clock_control_get_status` returns :c:enumerator:`CLOCK_CONTROL_STATUS_UNKNOWN`.

**Clock subsystem** (:c:type:`clock_control_subsys_t`)
  An opaque handle selecting one clock among those managed by a controller, such as the gate of a
  peripheral or an oscillator. The API never interprets it: drivers encode it either as an integer
  identifier cast to a pointer, usually taken from a devicetree specifier cell, or as a pointer to
  a vendor structure such as :c:struct:`stm32_pclken` or :c:struct:`litex_clk_setup`, declared
  with the other vendor extensions of the API under
  :zephyr_file:`include/zephyr/drivers/clock_control`. A handle is therefore only meaningful with
  the controller it was created for. :c:macro:`CLOCK_CONTROL_SUBSYS_ALL` designates all
  subsystems of a controller where supported.

**Clock status** (:c:enum:`clock_control_status`)
  Whether a subsystem is off, starting, on, or in an unknown state, as reported by
  :c:func:`clock_control_get_status`.

**Clock rate**
  The frequency of a subsystem in Hz, read with :c:func:`clock_control_get_rate` and, where
  supported, changed with :c:func:`clock_control_set_rate` through an opaque
  :c:type:`clock_control_subsys_rate_t` value.

Devicetree Configuration
************************

A clock provider is a node whose binding includes
:zephyr_file:`dts/bindings/clock/clock-controller.yaml` and therefore declares ``#clock-cells``,
the number of cells that follow its phandle in a clock specifier; the names of those cells are
listed under ``clock-cells`` in the binding (see :ref:`dt-bindings-cells`). A source with a single
fixed frequency can use the generic :dtcompatible:`fixed-clock` binding, which has no cells and a
``clock-frequency`` property and is served by the driver enabled with
:kconfig:option:`CONFIG_CLOCK_CONTROL_FIXED_RATE_CLOCK`.

Consumers list the clocks they need in a ``clocks`` phandle-array property (see
:ref:`phandle-properties`), optionally naming each entry in ``clock-names``. The following excerpt,
adapted from :zephyr_file:`dts/arm/st/f4/stm32f4.dtsi`, shows a fixed-rate oscillator, the RCC
clock controller with two cells per specifier, and a UART referencing its bus clock gate:

.. code-block:: devicetree

   clk_hsi: clk-hsi {
       compatible = "fixed-clock";
       #clock-cells = <0>;
       clock-frequency = <DT_FREQ_M(16)>;
   };

   rcc: rcc@40023800 {
       compatible = "st,stm32f4-rcc";
       reg = <0x40023800 0x400>;
       #clock-cells = <2>;
   };

   usart1: serial@40011000 {
       compatible = "st,stm32-usart", "st,stm32-uart";
       reg = <0x40011000 0x400>;
       clocks = <&rcc STM32_CLOCK(APB2, 4)>;
   };

Here ``STM32_CLOCK()`` expands to the ``bus`` and ``bits`` cells declared by
:dtcompatible:`st,stm32-rcc`: the bus the peripheral is attached to and the bit that gates its
clock. Controller nodes are usually consumers as well and carry the properties describing the clock
tree itself, such as the system clock source and bus prescalers of the RCC node. The
:ref:`clock_control_xec` sample shows the same approach for the 32 kHz clock of Microchip XEC SoCs.

In C, :c:macro:`DT_CLOCKS_CTLR` returns the node identifier of the controller referenced by a
``clocks`` entry and :c:macro:`DT_CLOCKS_CELL` (or :c:macro:`DT_CLOCKS_CELL_BY_NAME`) reads one
specifier cell; the ``DT_INST_CLOCKS_*`` variants do the same for a driver instance.

Typical Application Flow
************************

Typical use of the clock control API, whether from a peripheral driver or an application, is:

#. Obtain the clock controller device, normally with :c:macro:`DEVICE_DT_GET` on the node returned
   by :c:macro:`DT_CLOCKS_CTLR`, and check it with :c:func:`device_is_ready`.
#. Build the subsystem handle from the specifier cells or from the vendor header.
#. Optionally call :c:func:`clock_control_configure` to select a clock source or apply other
   driver-specific settings.
#. Start the clock with :c:func:`clock_control_on`, or with :c:func:`clock_control_async_on` when
   the caller must not block while the clock stabilizes.
#. Read the resulting frequency with :c:func:`clock_control_get_rate` and use it to program the
   peripheral, for example to compute baud rate divisors or timer periods.
#. Stop the clock with :c:func:`clock_control_off` when the peripheral is no longer in use,
   typically from its power management action.

Basic Operation
***************

The example below enables the clock of a peripheral described by the ``clocks`` property of node
``uart0`` and reads the resulting frequency. It follows the pattern of in-tree drivers such as
:zephyr_file:`drivers/serial/uart_mcux_lpuart.c`, where the specifier has a single ``name`` cell
holding the driver's subsystem identifier:

.. code-block:: c
   :caption: Enabling a peripheral clock referenced from devicetree

   #define UART_NODE DT_NODELABEL(uart0)

   static const struct device *const clk_dev = DEVICE_DT_GET(DT_CLOCKS_CTLR(UART_NODE));
   static const clock_control_subsys_t clk_subsys =
       (clock_control_subsys_t)DT_CLOCKS_CELL(UART_NODE, name);

   int uart_clock_enable(uint32_t *rate_hz)
   {
       int ret;

       if (!device_is_ready(clk_dev)) {
           return -ENODEV;
       }

       ret = clock_control_on(clk_dev, clk_subsys);
       if (ret < 0 && ret != -EALREADY) {
           return ret;
       }

       return clock_control_get_rate(clk_dev, clk_subsys, rate_hz);
   }

:c:func:`clock_control_on` returns once the clock is running. Calling it on a clock that is already
running is harmless: drivers return either ``0`` or ``-EALREADY`` and leave the clock on, and
:c:func:`clock_control_off` on a stopped clock returns ``0``, as verified by the
:zephyr_file:`tests/drivers/clock_control/clock_control_api` suite on several SoCs.

Asynchronous Start
==================

Some clocks, such as crystal oscillators, take a noticeable time to stabilize.
:c:func:`clock_control_async_on` requests the start and returns immediately; once the clock is
running, the driver invokes the :c:type:`clock_control_cb_t` callback with the ``user_data`` pointer
given at request time. Meanwhile :c:func:`clock_control_get_status` reports
:c:enumerator:`CLOCK_CONTROL_STATUS_STARTING`, and calling :c:func:`clock_control_off` cancels the
request so that the callback is never invoked.

.. code-block:: c
   :caption: Starting a clock without blocking

   static K_SEM_DEFINE(clk_started, 0, 1);

   static void clk_started_cb(const struct device *dev, clock_control_subsys_t subsys,
                              void *user_data)
   {
       k_sem_give(user_data);
   }

   int uart_clock_start_async(void)
   {
       int ret = clock_control_async_on(clk_dev, clk_subsys, clk_started_cb, &clk_started);

       if (ret < 0) {
           /* -EALREADY: the clock is already running or starting, no callback follows */
           return ret;
       }

       return k_sem_take(&clk_started, K_MSEC(100));
   }

Drivers typically invoke the callback from the controller's interrupt handler, so it must not block;
giving a semaphore or submitting a work item is the usual pattern. Drivers without asynchronous
support return ``-ENOSYS``, and :c:func:`clock_control_on` from thread context is the fallback.

Rates and Configuration
=======================

:c:func:`clock_control_get_rate` stores the current frequency of the subsystem in Hz: for a gated
peripheral clock, the frequency the peripheral is actually driven with after the source selection
and prescalers configured in devicetree. A driver may return ``-EAGAIN`` when the rate cannot be
determined while the clock is off, and ``-ENOTSUP`` for subsystems whose rate is not readable.

:c:func:`clock_control_set_rate` changes the frequency on controllers that support it, returning
``-EALREADY`` if the clock already runs at the requested rate. The rate is passed as an opaque
:c:type:`clock_control_subsys_rate_t` whose meaning is driver-specific: the generic
:dtcompatible:`pwm-clock` driver expects the frequency in Hz cast to that type, while the LiteX
driver takes rate, phase and duty cycle from the :c:struct:`litex_clk_setup` handle and applies
them in :c:func:`clock_control_on`, as shown in :zephyr:code-sample:`clock-control-litex`.

:c:func:`clock_control_configure` applies driver-specific settings through an opaque ``data``
pointer. The STM32 driver uses it to select the domain clock source of a peripheral, as described
in :dtcompatible:`st,stm32-rcc`: the peripheral lists an additional ``clocks`` entry naming the
source and its driver passes that entry to :c:func:`clock_control_configure` at initialization.
Other consumers pass ``NULL`` when no extra input is needed and, since the call returns ``-ENOSYS``
on drivers that do not implement it, treat that value as success. After selecting a source, later
calls must use the same subsystem so that :c:func:`clock_control_get_rate` reports the rate of the
clock in use.

Usage Constraints
*****************

:c:func:`clock_control_on` and :c:func:`clock_control_set_rate` may sleep while the hardware
stabilizes and must only be called from thread context. :c:func:`clock_control_off`,
:c:func:`clock_control_async_on` and :c:func:`clock_control_configure` are non-blocking and may be
called from any context. The API adds no locking or reference counting of its own.

All functions return ``0`` on success and a negative errno value otherwise: ``-ENOSYS`` when the
driver does not implement the operation, ``-EALREADY`` when the clock is already running, starting
or at the requested rate, ``-ENOTSUP`` when the request cannot be satisfied for this subsystem, and
other driver-specific values.

The generic API does not count the users of a clock: a subsystem shared by several peripherals is
switched off by the first :c:func:`clock_control_off` call unless the driver keeps its own
bookkeeping. The Nordic nRF drivers, for example, arbitrate their oscillators through the
:ref:`on-off manager <resource_mgmt_onoff>`; see :zephyr:code-sample:`nrf_clock_control`.

Gating clocks is the main way device drivers save power: a driver supporting
:ref:`device power management <pm-device-runtime>` turns its clock off in its
``PM_DEVICE_ACTION_SUSPEND`` handler and back on in ``PM_DEVICE_ACTION_RESUME``, as
:zephyr_file:`drivers/i2c/i2c_mcux_lpi2c.c` does. Clock controllers must initialize before their
consumers: most drivers use :kconfig:option:`CONFIG_CLOCK_CONTROL_INIT_PRIORITY` as their
initialization priority, and consumers check :c:func:`device_is_ready` before using a controller.

The clock control API only controls clocks. Observing a running clock for frequency drift or loss
is the role of the :ref:`clock_monitor_api`.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_CLOCK_CONTROL`
* :kconfig:option:`CONFIG_CLOCK_CONTROL_INIT_PRIORITY`
* :kconfig:option:`CONFIG_CLOCK_CONTROL_FIXED_RATE_CLOCK`
* :kconfig:option:`CONFIG_CLOCK_CONTROL_PWM`

API Reference
*************

.. doxygengroup:: clock_control_interface
