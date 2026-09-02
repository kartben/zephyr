.. _spi_api:

Serial Peripheral Interface (SPI) Bus
#####################################

Overview
********

The Serial Peripheral Interface (SPI) is a synchronous serial bus. A controller drives the clock
(SCK) and one chip select (CS) line per peripheral, and data is shifted out on MOSI and in on MISO
while the chip select is asserted; in full duplex mode every word clocked out is matched by one
word clocked in.

The Zephyr SPI API abstracts the controller hardware. The application describes what a peripheral
needs (clock frequency, polarity and phase, word size, bit order, chip select) and hands the
controller driver buffers to exchange; the driver configures the hardware, drives the chip select,
moves the data by polling, interrupts or DMA, and serializes bus access between threads. Key
concepts include:

**Configuration** (:c:struct:`spi_config`)
  The bus frequency, chip select number and control, delay between words, and the operation flags,
  an :c:type:`spi_operation_t` bit field built from macros such as :c:macro:`SPI_OP_MODE_MASTER`,
  :c:macro:`SPI_MODE_CPOL`, :c:macro:`SPI_MODE_CPHA` and :c:macro:`SPI_WORD_SET`.

**Devicetree specification** (:c:struct:`spi_dt_spec`)
  The bus device pointer and the configuration of one SPI device, populated from its devicetree
  node with :c:macro:`SPI_DT_SPEC_GET` and used by the ``_dt`` variants of the transfer functions.

**Buffers** (:c:struct:`spi_buf`, :c:struct:`spi_buf_set`)
  One array of buffers for transmission and one for reception, walked in order while the chip
  select stays asserted, which gives scatter-gather transfers without copying data.

**Transfer functions** (:c:func:`spi_transceive`, :c:func:`spi_write`, :c:func:`spi_read`)
  Synchronous transfers that return once the data has been exchanged, with optional asynchronous
  (:c:func:`spi_transceive_cb`, :c:func:`spi_transceive_signal`) and RTIO variants.

**Chip select control** (:c:struct:`spi_cs_control`)
  A GPIO listed in the controller's ``cs-gpios`` property or the controller's own chip select
  logic. :c:macro:`SPI_HOLD_ON_CS` and :c:macro:`SPI_LOCK_ON` extend a transaction over several
  calls and :c:func:`spi_release` ends it.

Configuration
*************

:c:member:`spi_config.frequency` is the bus clock in Hz, :c:member:`spi_config.slave` the chip
select number of the peripheral on its controller (the ``reg`` address of its node),
:c:member:`spi_config.cs` the chip select control, and :c:member:`spi_config.word_delay` the delay
between words in nanoseconds, where zero means half a clock period.
:c:member:`spi_config.operation` combines the following flags:

* Role: :c:macro:`SPI_OP_MODE_MASTER` (the default, with value 0) or, with
  :kconfig:option:`CONFIG_SPI_SLAVE` (experimental, driver and hardware dependent),
  :c:macro:`SPI_OP_MODE_SLAVE`. In peripheral mode the remote controller drives the clock and the
  chip select, the driver waits for it without timeout, and on success the transfer functions
  return the number of frames received instead of 0. The
  :zephyr_file:`tests/drivers/spi/spi_controller_peripheral` test wires two SPI instances of one
  board together, one in each role.
* Clock mode: :c:macro:`SPI_MODE_CPOL` makes the clock idle high and :c:macro:`SPI_MODE_CPHA`
  captures data on the second clock edge; SPI modes 0 to 3 are no flag, ``SPI_MODE_CPHA``,
  ``SPI_MODE_CPOL`` and both. :c:macro:`SPI_MODE_LOOP` enables hardware loopback where supported.
* Word size and bit order: :c:macro:`SPI_WORD_SET` encodes the size of a word (data frame) in bits
  and :c:macro:`SPI_TRANSFER_LSB` selects LSB first instead of the default
  :c:macro:`SPI_TRANSFER_MSB`. Words of 9 to 16 bits are stored in ``uint16_t`` arrays and wider
  words in ``uint32_t`` arrays; buffer lengths are always given in bytes.
* Duplex and frame format: :c:macro:`SPI_HALF_DUPLEX` (three-wire) instead of the default
  :c:macro:`SPI_FULL_DUPLEX`, and :c:macro:`SPI_FRAME_FORMAT_TI` instead of the default
  :c:macro:`SPI_FRAME_FORMAT_MOTOROLA`, both usable in devicetree as well.
* Transfer control: :c:macro:`SPI_CS_ACTIVE_HIGH`, :c:macro:`SPI_HOLD_ON_CS` and
  :c:macro:`SPI_LOCK_ON`, described in :ref:`spi_chip_select` and :ref:`spi_lock_hold`.
* Data lines: :c:macro:`SPI_LINES_DUAL`, :c:macro:`SPI_LINES_QUAD` and :c:macro:`SPI_LINES_OCTAL`
  occupy bits 16 and 17, which only exist when :kconfig:option:`CONFIG_SPI_EXTENDED_MODES` widens
  :c:type:`spi_operation_t` from 16 to 32 bits; even then the SPI drivers do not implement
  multi-line transfers, and multi-bit controllers are covered by the :ref:`MSPI <mspi_api>` API.

.. warning::

   Most drivers compare the address of the :c:struct:`spi_config` with the one used by the previous
   transfer to decide whether to reconfigure the hardware. Use a different configuration object for
   different settings instead of modifying the fields of one that has already been used.

.. _spi_chip_select:

Chip Select
***********

When the controller node has a ``cs-gpios`` property, the entry at index ``reg`` of a device node is
the GPIO that selects that device, and the driver toggles it through the :ref:`GPIO API <gpio_api>`.
:c:macro:`SPI_DT_SPEC_GET` and :c:macro:`SPI_CS_CONTROL_INIT` fill :c:member:`spi_cs_control.gpio`
from that entry and set :c:member:`spi_cs_control.delay`, in microseconds, from the larger of the
device's ``spi-cs-setup-delay-ns`` and ``spi-cs-hold-delay-ns`` properties; the driver waits that
long after asserting the chip select and again before releasing it. Without ``cs-gpios`` the
controller's own chip select logic is used and the same properties initialize
:c:member:`spi_cs_control.setup_ns` and :c:member:`spi_cs_control.hold_ns` instead.
:c:func:`spi_cs_is_gpio_dt` tells which form is in use and :c:func:`spi_is_ready_dt` checks that
both the bus and the chip select GPIO port are ready. The ``spi-cs-high`` property sets
:c:macro:`SPI_CS_ACTIVE_HIGH`; a GPIO chip select takes its polarity from its devicetree flags
(``GPIO_ACTIVE_LOW`` for the usual ``CSn`` pin), which should agree with that flag. In controller
mode the chip select stays asserted for all buffers of a transfer and is released after the last
one unless :c:macro:`SPI_HOLD_ON_CS` is set.

Devicetree Configuration
************************

SPI controllers are described by nodes whose binding includes
:zephyr_file:`dts/bindings/spi/spi-controller.yaml` and SPI peripherals by child nodes whose
binding includes :zephyr_file:`dts/bindings/spi/spi-device.yaml`. A controller node has
``#address-cells = <1>``, ``#size-cells = <0>``, an optional ``cs-gpios`` array and an optional
``overrun-character``, the value clocked out on MOSI once the transmit data is exhausted while
reception continues (:c:macro:`SPI_MOSI_OVERRUN_DT` reads it). A device node needs ``reg`` (its
chip select number) and ``spi-max-frequency``. :c:macro:`SPI_CONFIG_DT` turns the optional
``spi-cpol``, ``spi-cpha``, ``spi-lsb-first``, ``spi-cs-high``, ``spi-hold-cs``, ``duplex`` and
``frame-format`` properties into operation flags and merges them with the flags given by the
application, typically the word size and bit order; ``spi-interframe-delay-ns`` sets
:c:member:`spi_config.word_delay` and ``spi-cs-setup-delay-ns`` and ``spi-cs-hold-delay-ns``
initialize the chip select control as described above.

.. code-block:: devicetree
   :caption: SPI controller with a GPIO chip select, adapted from the Thingy:53 board

   &spi3 {
           compatible = "nordic,nrf-spim";
           status = "okay";
           cs-gpios = <&gpio0 22 GPIO_ACTIVE_LOW>;

           adxl362: spi-dev-adxl362@0 {
                   compatible = "adi,adxl362";
                   reg = <0>;
                   spi-max-frequency = <8000000>;
           };
   };

Vendor bindings such as :dtcompatible:`nordic,nrf-spim` and :dtcompatible:`st,stm32-spi` add
``pinctrl`` states, DMA channels and timing details; the matching driver is enabled by default and
driver specific options such as :kconfig:option:`CONFIG_SPI_STM32_DMA` select the transfer
mechanism. The SPI section of the :ref:`devicetree_api` page lists helper macros such as
:c:macro:`DT_SPI_DEV_HAS_CS_GPIOS`. Boards without a spare hardware controller can use the GPIO
bit-banged controller (:dtcompatible:`zephyr,spi-bitbang`, :kconfig:option:`CONFIG_SPI_BITBANG`),
whose node names the ``clk-gpios``, ``mosi-gpios`` and ``miso-gpios`` pins next to ``cs-gpios``;
its rate is limited by the GPIO calls, but it handles word sizes that are not multiples of 8 bits,
as the :zephyr:code-sample:`spi-bitbang` sample shows.

Typical application flow
************************

#. Describe the peripheral in devicetree as a child of the controller node, with its ``reg`` chip
   select number, ``spi-max-frequency`` and any clock mode properties, and list its chip select
   GPIO in the controller's ``cs-gpios`` when the controller does not drive it natively.
#. Enable :kconfig:option:`CONFIG_SPI`, plus :kconfig:option:`CONFIG_SPI_ASYNC` or
   :kconfig:option:`CONFIG_SPI_RTIO` for the asynchronous paths.
#. Define a :c:struct:`spi_dt_spec` with :c:macro:`SPI_DT_SPEC_GET`, passing the operation flags
   the peripheral requires, and check it with :c:func:`spi_is_ready_dt`.
#. Describe the data with :c:struct:`spi_buf` arrays wrapped in :c:struct:`spi_buf_set`.
#. Call :c:func:`spi_transceive_dt`, :c:func:`spi_write_dt` or :c:func:`spi_read_dt`.
#. When a transfer used :c:macro:`SPI_LOCK_ON` or :c:macro:`SPI_HOLD_ON_CS`, end the transaction
   with :c:func:`spi_release_dt`.

Basic Operation
***************

Many SPI peripherals expect a command or register address before the data. The example below reads
registers from the ``adxl362`` node above: the bytes received while the command is clocked out are
discarded through a ``NULL`` receive buffer of the same length, and the register contents land in
``data``. A ``NULL`` transmit buffer sends zeros for its length, and when the receive set is longer
than the transmit set the controller keeps clocking while driving MOSI with its overrun character.

.. code-block:: c
   :caption: Reading registers from a device defined in devicetree

   #define ADXL362_READ_REG 0x0B

   static const struct spi_dt_spec sensor = SPI_DT_SPEC_GET(
           DT_NODELABEL(adxl362), SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);

   static int sensor_read_regs(uint8_t reg, uint8_t *data, size_t len)
   {
           uint8_t cmd[2] = { ADXL362_READ_REG, reg };
           const struct spi_buf tx_bufs[] = {
                   { .buf = cmd, .len = sizeof(cmd) },
           };
           const struct spi_buf rx_bufs[] = {
                   { .buf = NULL, .len = sizeof(cmd) },
                   { .buf = data, .len = len },
           };
           const struct spi_buf_set tx = { .buffers = tx_bufs, .count = ARRAY_SIZE(tx_bufs) };
           const struct spi_buf_set rx = { .buffers = rx_bufs, .count = ARRAY_SIZE(rx_bufs) };

           if (!spi_is_ready_dt(&sensor)) {
                   return -ENODEV;
           }

           return spi_transceive_dt(&sensor, &tx, &rx);
   }

:c:func:`spi_transceive_dt` returns 0 on success in controller mode, ``-EINVAL`` when a parameter of
the configuration is invalid, ``-ENOTSUP`` when the configuration is not supported by the hardware
or the driver, and, for drivers built on the common context helper, ``-ETIMEDOUT`` when the transfer
does not finish within the time expected from its length and frequency plus
:kconfig:option:`CONFIG_SPI_COMPLETION_TIMEOUT_TOLERANCE`. :c:func:`spi_write_dt` and
:c:func:`spi_read_dt` pass ``NULL`` for the unused set, and the same set may be given for both
directions. :c:func:`spi_transceive` and :c:func:`spi_release` are system calls, usable from
:ref:`user mode <usermode_api>` threads with fewer than 32 buffers per set. The driver behind the
:zephyr:code-sample:`spi-nor` sample is a typical bus client built with
:c:macro:`SPI_DT_SPEC_INST_GET`.

Asynchronous Transfers
======================

With :kconfig:option:`CONFIG_SPI_ASYNC`, :c:func:`spi_transceive_cb` returns as soon as the
transfer is under way and invokes its :c:type:`spi_callback_t` callback with the result when it
ends, possibly from interrupt context. :c:func:`spi_transceive_signal`, :c:func:`spi_read_signal`
and :c:func:`spi_write_signal` instead raise a ``struct k_poll_signal`` whose ``result`` field
carries the status, which a thread waits for with :c:func:`k_poll` (see :ref:`polling_v2`). The
asynchronous entry point is optional in drivers: some, such as the bit-banged driver, only return
``-ENOTSUP`` from it, and the API does not check whether a driver provides it at all. These
functions still block while another transfer owns the bus, and they have no ``_dt`` variants:

.. code-block:: c

   static struct k_poll_signal async_sig = K_POLL_SIGNAL_INITIALIZER(async_sig);
   static struct k_poll_event async_evt =
           K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &async_sig);

   ret = spi_transceive_signal(sensor.bus, &sensor.config, &tx, &rx, &async_sig);
   if (ret == 0) {
           k_poll(&async_evt, 1, K_FOREVER);
           ret = async_sig.result;
           k_poll_signal_reset(&async_sig);
           async_evt.state = K_POLL_STATE_NOT_READY;
   }

RTIO Transfers
==============

:kconfig:option:`CONFIG_SPI_RTIO` (experimental) exposes SPI devices to the :ref:`RTIO <rtio>`
framework, which queues several operations, chains them and reports completions without a thread
blocking per transfer. :c:macro:`SPI_DT_IODEV_DEFINE` defines an I/O device bound to the
:c:struct:`spi_dt_spec` of a devicetree node and :c:func:`spi_is_ready_iodev` checks it.
Submissions are prepared with :c:func:`rtio_sqe_prep_transceive`, :c:func:`rtio_sqe_prep_write` or
:c:func:`rtio_sqe_prep_read` and started with :c:func:`rtio_submit`; submissions linked with
:c:macro:`RTIO_SQE_TRANSACTION` are executed as a single SPI transfer, so the chip select stays
asserted across them. Drivers without native RTIO support fall back to a handler that converts the
submissions of a transaction into buffer sets (at most
:kconfig:option:`CONFIG_SPI_RTIO_FALLBACK_MSGS` of them) and runs :c:func:`spi_transceive` on the
RTIO work queue. See the :zephyr:code-sample:`spi-rtio-loopback` sample. RTIO is also the
recommended way to chain transfers, rather than starting a new one from a completion callback.

.. _spi_lock_hold:

Holding the Chip Select and Locking the Bus
*******************************************

Some protocols cannot be expressed as one buffer set, for example a variable length packet whose
length must be read before the payload is requested. Two flags in :c:member:`spi_config.operation`
let a transaction span several calls:

* :c:macro:`SPI_HOLD_ON_CS` keeps the chip select asserted when a transfer ends, if the controller
  supports it. The next transfer with the same configuration starts with the chip select already
  asserted, and a transfer made with the flag cleared ends with the chip select released. The
  ``spi-hold-cs`` devicetree property sets the flag for a device.
* :c:macro:`SPI_LOCK_ON` keeps the controller locked for the caller after the transfer, so that
  transfers requested with any other configuration wait until the lock is released. The owner is
  identified by the address of the :c:struct:`spi_config`, which therefore has to be reused for the
  following transfers and for the release.

:c:func:`spi_release` (or :c:func:`spi_release_dt`) releases the lock and deasserts the chip select
when the given configuration was the last one used and carries one of these flags. The two flags
are usually combined so that no other bus user can slip in while the chip select is held. Drivers
implemented natively on RTIO, such as the one enabled by
:kconfig:option:`CONFIG_SPI_NRFX_SPIM_RTIO`, do not support ``SPI_LOCK_ON`` and return
``-ENOTSUP`` from :c:func:`spi_release`.

Usage Constraints
*****************

* The transfer functions may sleep while waiting for the bus and must not be called from an ISR or
  from an asynchronous completion callback; chain transfers through RTIO instead.
* Transfers on one controller are serialized by the driver, so several threads can use the same bus
  with the same or different configurations without additional locking.
* Buffers must stay valid until the transfer completes. Drivers that use DMA need buffers in RAM
  and may require cache coherent memory; the :zephyr_file:`tests/drivers/spi/spi_loopback` test
  places its buffers in a ``.nocache`` section when :kconfig:option:`CONFIG_NOCACHE_MEMORY` is set.
* Controller drivers that implement :ref:`device runtime power management <pm-device-runtime>`
  resume the hardware for a transfer and suspend it afterwards; an application that performs many
  back-to-back transfers can keep the controller powered with :c:func:`pm_device_runtime_get`.
* :kconfig:option:`CONFIG_SPI_STATS` records the transmitted and received byte counts and the
  number of failed transfers of every controller in the statistics subsystem.
* Controller drivers register their instances with :c:macro:`SPI_DEVICE_DT_DEFINE` and implement
  :c:struct:`spi_driver_api`, whose ``transceive`` and ``release`` entries are mandatory. Most
  in-tree drivers build on the private helpers of :zephyr_file:`drivers/spi/spi_context.h` for
  locking, completion, chip select and buffer handling.

Shell commands
**************

When :kconfig:option:`CONFIG_SPI_SHELL` is enabled, the ``spi`` command exchanges bytes with SPI
devices from the :ref:`shell <shell_api>`. ``<spi-device>`` is the node label or device name of an
SPI device from devicetree, or the name (or, with :kconfig:option:`CONFIG_DEVICE_DT_METADATA`, a
node label) of an SPI controller. Controllers start with a default configuration of 1 MHz, 8-bit
words and controller mode, and :kconfig:option:`CONFIG_SPI_SHELL_MAX_DEVICE_SLOTS` sets how many of
them the shell can register. Devices start with the settings of their devicetree node but without a
word size, so configure them with ``spi conf`` before the first transfer.

``spi conf <spi-device> <frequency> [<settings>]``
  Set the bus frequency (100000 to 80000000 Hz) and the operation flags. The flags are reset to
  8-bit words in controller mode, extended by the letters in ``settings``: ``o`` for
  ``SPI_MODE_CPOL``, ``h`` for ``SPI_MODE_CPHA``, ``l`` for ``SPI_TRANSFER_LSB`` and ``T`` for
  ``SPI_FRAME_FORMAT_TI``. For example, ``spi conf spi1 1000000 ol`` selects SPI mode 2, LSB first.

``spi cs <spi-device> <gpio-device> <pin> [<flags>]``
  Use pin ``pin`` of the GPIO controller ``gpio-device`` as chip select for the device, with
  optional GPIO flags in hexadecimal, for example ``spi cs spi1 gpio1 3 0x01`` for active low.

``spi transceive <spi-device> <tx-byte1> [<tx-byte2> ...]``
  Send the given bytes (hexadecimal) in one transfer and print hex dumps of the transmitted and the
  received data, for example ``spi transceive spi1 0x00 0x01``. At most 32 bytes can be sent, or
  fewer when :kconfig:option:`CONFIG_SHELL_ARGC_MAX` is small.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_SPI`
* :kconfig:option:`CONFIG_SPI_ASYNC`
* :kconfig:option:`CONFIG_SPI_RTIO`
* :kconfig:option:`CONFIG_SPI_RTIO_FALLBACK_MSGS`
* :kconfig:option:`CONFIG_SPI_SLAVE`
* :kconfig:option:`CONFIG_SPI_EXTENDED_MODES`
* :kconfig:option:`CONFIG_SPI_INIT_PRIORITY`
* :kconfig:option:`CONFIG_SPI_COMPLETION_TIMEOUT_TOLERANCE`
* :kconfig:option:`CONFIG_SPI_STATS`
* :kconfig:option:`CONFIG_SPI_SHELL`
* :kconfig:option:`CONFIG_SPI_SHELL_MAX_DEVICE_SLOTS`
* :kconfig:option:`CONFIG_SPI_BITBANG`

API Reference
*************

.. doxygengroup:: spi_interface
