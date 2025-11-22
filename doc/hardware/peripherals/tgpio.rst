.. _tgpio_api:


Time-aware General-Purpose Input/Output (TGPIO)
###############################################

Overview
********

Time-aware GPIO (TGPIO) extends standard GPIO functionality with precise hardware timestamping
and time-synchronized output generation. TGPIO is particularly useful for applications requiring
accurate timing synchronization, such as network time protocols (PTP/IEEE 1588), industrial
automation, and synchronized multi-device systems.

The TGPIO API provides an interface for time-aware GPIO operations. Key features include:

**Precise Timestamping**
  Capture input events with hardware timestamps.
  Nanosecond-level timing accuracy using hardware timers.
  Event counting for tracking pulse occurrences.

**Periodic Output Generation**
  Generate periodic pulses at precise time intervals.
  Schedule output events at specific absolute times.
  Hardware-based pulse generation for minimal jitter.

**Clock Synchronization**
  Integration with Always Running Timer (ART) or similar hardware clocks.
  Support for clock synchronization protocols (e.g., PTP).
  Query current time and clock frequency.

**Edge Detection**
  Configure rising edge, falling edge, or toggle detection.
  Flexible polarity configuration for input capture.

Devicetree Configuration
************************

TGPIO devices are defined in the Devicetree with a time-aware GPIO compatible property.
The configuration depends on the hardware platform.

Example of a TGPIO device definition:

.. code-block:: devicetree

   tgpio: tgpio@fe001200 {
       compatible = "intel,timeaware-gpio";
       reg = <0xfe001200 0x100>;
       interrupts = <14 IRQ_TYPE_LEVEL IRQ_DEFAULT_PRIORITY>;
       status = "okay";
   };

Example of referencing a TGPIO device:

.. code-block:: devicetree

   aliases {
       tgpio0 = &tgpio;
   };

Basic Operation
***************

TGPIO operations typically involve configuring pins for timestamping input events
or generating time-synchronized output pulses.

.. code-block:: c
   :caption: Configure TGPIO for periodic output and input timestamping

   #include <zephyr/drivers/misc/timeaware_gpio/timeaware_gpio.h>

   #define TGPIO_NODE DT_NODELABEL(tgpio)

   void setup_tgpio(void)
   {
       const struct device *tgpio_dev = DEVICE_DT_GET(TGPIO_NODE);
       uint64_t current_time, start_time;
       uint64_t timestamp, event_count;
       uint32_t cycles_per_sec;
       int ret;

       if (!device_is_ready(tgpio_dev)) {
           printk("TGPIO device not ready\n");
           return;
       }

       /* Get current time */
       ret = tgpio_port_get_time(tgpio_dev, &current_time);
       if (ret < 0) {
           printk("Failed to get time: %d\n", ret);
           return;
       }

       /* Get clock frequency */
       ret = tgpio_port_get_cycles_per_second(tgpio_dev, &cycles_per_sec);
       if (ret < 0) {
           printk("Failed to get cycles per second: %d\n", ret);
           return;
       }

       printk("TGPIO time: %llu, frequency: %u Hz\n", current_time, cycles_per_sec);

       /* Configure periodic output on pin 0 */
       start_time = current_time + cycles_per_sec; /* Start in 1 second */
       uint64_t interval = cycles_per_sec / 10;    /* 10 Hz periodic signal */
       ret = tgpio_pin_periodic_output(tgpio_dev, 0, start_time, interval, true);
       if (ret < 0) {
           printk("Failed to configure periodic output: %d\n", ret);
           return;
       }

       /* Configure input timestamping on pin 1 */
       ret = tgpio_pin_config_ext_timestamp(tgpio_dev, 1, TGPIO_RISING_EDGE);
       if (ret < 0) {
           printk("Failed to configure timestamp input: %d\n", ret);
           return;
       }

       /* Read timestamp and event count */
       k_sleep(K_MSEC(100));
       ret = tgpio_pin_read_ts_ec(tgpio_dev, 1, &timestamp, &event_count);
       if (ret < 0) {
           printk("Failed to read timestamp: %d\n", ret);
           return;
       }

       printk("Timestamp: %llu, Event count: %llu\n", timestamp, event_count);
   }

Refer to :zephyr:code-sample:`timeaware-gpio` for a complete example of using the TGPIO API.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_TIMEAWARE_GPIO`

API Reference
*************

.. doxygengroup:: tgpio_interface
