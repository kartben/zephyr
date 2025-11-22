.. _spi_api:

Serial Peripheral Interface (SPI) Bus
#####################################

Overview
********

Serial Peripheral Interface (SPI) is a synchronous serial communication protocol used for
short-distance communication between microcontrollers and peripheral devices. SPI uses a
controller-peripheral architecture with separate lines for clock (CLK), data out (MOSI),
data in (MISO), and chip select (CS).

The SPI API provides a generic interface for SPI controllers and peripherals. Key features include:

**Controller and Peripheral Modes**
  Controller mode: controls the clock and initiates transactions.
  Peripheral mode: responds to controller-initiated transactions.

**Flexible Configuration**
  Configurable clock polarity (CPOL) and phase (CPHA).
  Multiple word sizes (typically 8 or 16 bits).
  Variable clock frequencies.
  Multiple chip select lines for device selection.

**Transfer Operations**
  Synchronous and asynchronous transfers.
  Full-duplex, half-duplex, and simplex communication.
  Support for scatter-gather operations.

**RTIO Integration**
  Real-Time I/O (RTIO) support for efficient high-speed data transfers.
  Integration with DMA for improved performance.

Devicetree Configuration
************************

SPI controllers are defined in the Devicetree with the ``spi`` compatible property.
SPI devices (peripherals) are defined as child nodes of the SPI controller.

Example of an SPI controller definition:

.. code-block:: devicetree

   spi0: spi@40013000 {
       compatible = "st,stm32-spi";
       reg = <0x40013000 0x400>;
       interrupts = <35 5>;
       clocks = <&rcc STM32_CLOCK_BUS_APB2 0x00001000>;
       #address-cells = <1>;
       #size-cells = <0>;
       status = "okay";
   };

Example of an SPI device (peripheral) definition:

.. code-block:: devicetree

   &spi0 {
       cs-gpios = <&gpioa 4 GPIO_ACTIVE_LOW>;

       spi_device: spi-device@0 {
           compatible = "vendor,spi-device";
           reg = <0>;
           spi-max-frequency = <1000000>;
       };
   };

Basic Operation
***************

SPI operations are performed using a :c:struct:`spi_config` structure that defines
the communication parameters, and buffer structures for transmit and receive data.

.. code-block:: c
   :caption: Configure and perform an SPI transaction

   #include <zephyr/drivers/spi.h>

   #define SPI_NODE DT_NODELABEL(spi0)

   static const struct device *spi_dev = DEVICE_DT_GET(SPI_NODE);

   void spi_transaction(void)
   {
       struct spi_config spi_cfg = {
           .frequency = 1000000,  /* 1 MHz */
           .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
           .slave = 0,
       };
       uint8_t tx_buffer[] = {0x01, 0x02, 0x03};
       uint8_t rx_buffer[3];
       struct spi_buf tx_buf = {
           .buf = tx_buffer,
           .len = sizeof(tx_buffer),
       };
       struct spi_buf rx_buf = {
           .buf = rx_buffer,
           .len = sizeof(rx_buffer),
       };
       struct spi_buf_set tx_bufs = {
           .buffers = &tx_buf,
           .count = 1,
       };
       struct spi_buf_set rx_bufs = {
           .buffers = &rx_buf,
           .count = 1,
       };
       int ret;

       if (!device_is_ready(spi_dev)) {
           printk("SPI device not ready\n");
           return;
       }

       /* Perform SPI transaction */
       ret = spi_transceive(spi_dev, &spi_cfg, &tx_bufs, &rx_bufs);
       if (ret < 0) {
           printk("SPI transceive failed: %d\n", ret);
           return;
       }

       printk("SPI transaction successful\n");
   }

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_SPI`
* :kconfig:option:`CONFIG_SPI_ASYNC`
* :kconfig:option:`CONFIG_SPI_RTIO`
* :kconfig:option:`CONFIG_SPI_SLAVE`
* :kconfig:option:`CONFIG_SPI_EXTENDED_MODES`

API Reference
*************

.. doxygengroup:: spi_interface
