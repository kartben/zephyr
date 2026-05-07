.. _spi_api:

Serial Peripheral Interface (SPI) Bus
#####################################

Overview
********

The Serial Peripheral Interface (SPI) is a synchronous bus used to exchange data between one
controller and one or more peripheral devices. A typical SPI bus uses a clock line (SCK), data
lines for controller-to-peripheral (MOSI) and peripheral-to-controller (MISO) transfers, and a chip
select line for each peripheral.

SPI transfers are usually full-duplex: data is shifted out and sampled in at the same time. Devices
that only need to read or write still use the same transfer mechanism, with either the transmit or
receive buffers omitted.

The SPI API provides a generic interface for configuring a peripheral on an SPI bus and performing
synchronous, asynchronous, or RTIO-based transfers.

Key concepts
************

**SPI controller**
  The Zephyr device that drives the SPI bus clock and performs transfers. In devicetree, the
  controller is the SPI bus node.

**SPI peripheral**
  A device connected to an SPI controller. In Devicetree, each peripheral is represented as a child
  node of the SPI controller. The child node describes the peripheral address, maximum bus
  frequency, clock mode, and chip-select requirements.

**Operation configuration**
  SPI settings are collected in :c:struct:`spi_config`. Common settings include the bus frequency,
  word size, bit order, clock polarity and phase, duplex mode, frame format, and chip-select
  behavior.

**Transfer buffers**
  Data is transferred using :c:struct:`spi_buf` and :c:struct:`spi_buf_set`. A buffer set may contain
  one or more buffers, allowing scatter-gather transfers without copying data into one contiguous
  block.

Devicetree Configuration
************************

SPI controllers are defined as bus nodes in devicetree. SPI peripherals are child nodes of the
controller. The child node ``reg`` property selects the peripheral address or chip-select index, and
``spi-max-frequency`` sets the maximum clock frequency supported by that peripheral.

Chip selects can be provided by controller hardware or by GPIOs. When GPIO chip selects are used,
the controller node defines ``cs-gpios`` and each child node selects an entry with its ``reg`` value.

Example devicetree overlay:

.. code-block:: devicetree

   &spi1 {
       status = "okay";
       cs-gpios = <&gpio0 15 GPIO_ACTIVE_LOW>;

       sensor: sensor@0 {
           compatible = "vnd,spi-device";
           reg = <0>;
           spi-max-frequency = <1000000>;
           spi-cpol;
           spi-cpha;
       };
   };

Basic Operation
***************

SPI operations are usually performed on a :c:struct:`spi_dt_spec` structure, which contains both the
SPI controller device and the peripheral-specific :c:struct:`spi_config`.

This structure is typically populated using :c:macro:`SPI_DT_SPEC_GET`.

.. code-block:: c
   :caption: Populating a spi_dt_spec for a SPI peripheral

   #define SPI_PERIPHERAL_NODE DT_NODELABEL(sensor)

   static const struct spi_dt_spec spi_dev =
       SPI_DT_SPEC_GET(SPI_PERIPHERAL_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB);

Before using the device, check that the controller and any GPIO chip-select device are ready:

.. code-block:: c

   if (!spi_is_ready_dt(&spi_dev)) {
       return -ENODEV;
   }

The same :c:struct:`spi_dt_spec` can then be used with the devicetree helper functions.

.. code-block:: c
   :caption: Performing a full-duplex SPI transfer

   uint8_t tx_data[] = { 0x9f };
   uint8_t rx_data[3];
   const struct spi_buf tx_bufs[] = {
       {
           .buf = tx_data,
           .len = sizeof(tx_data),
       },
   };
   const struct spi_buf rx_bufs[] = {
       {
           .buf = rx_data,
           .len = sizeof(rx_data),
       },
   };
   const struct spi_buf_set tx = {
       .buffers = tx_bufs,
       .count = ARRAY_SIZE(tx_bufs),
   };
   const struct spi_buf_set rx = {
       .buffers = rx_bufs,
       .count = ARRAY_SIZE(rx_bufs),
   };
   int ret;

   ret = spi_transceive_dt(&spi_dev, &tx, &rx);
   if (ret < 0) {
       return ret;
   }

Use :c:func:`spi_write_dt` for write-only transfers and :c:func:`spi_read_dt` for read-only
transfers.

SPI operations can also be performed directly on an SPI controller device, in which case you will
use the SPI API functions that take a device pointer and :c:struct:`spi_config` pointer as
arguments. For example, use :c:func:`spi_transceive` instead of :c:func:`spi_transceive_dt`.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_SPI`
* :kconfig:option:`CONFIG_SPI_ASYNC`
* :kconfig:option:`CONFIG_SPI_RTIO`
* :kconfig:option:`CONFIG_SPI_SLAVE`
* :kconfig:option:`CONFIG_SPI_EXTENDED_MODES`
* :kconfig:option:`CONFIG_SPI_SHELL`
* :kconfig:option:`CONFIG_SPI_STATS`

API Reference
*************

.. doxygengroup:: spi_interface
