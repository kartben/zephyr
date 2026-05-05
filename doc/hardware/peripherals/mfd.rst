.. _mfd_api:

Multi-Function Device (MFD)
###########################

Overview
********

Multi-function devices (MFDs) are ICs that integrate several distinct
peripherals — for example a PMIC that combines regulators, a fuel gauge,
GPIO expanders, and an RTC — behind a single bus connection. In Zephyr, the
MFD driver layer provides shared, serialised access to the underlying bus
(typically I2C or SPI) so that the individual child drivers for each function
can coexist safely.

Each MFD driver is specific to the IC it supports. Child drivers bind to the
MFD parent via devicetree and call device-specific APIs (defined in
``include/zephyr/drivers/mfd/``) to read and write registers. The MFD driver
handles bus locking so that concurrent access from multiple child drivers is
safe.

Because there is no single generic MFD API, there is no unified API
Reference section here. Refer to the devicetree binding and the header file
under ``include/zephyr/drivers/mfd/`` for the specific IC you are using.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_MFD`
* :kconfig:option:`CONFIG_MFD_INIT_PRIORITY`
