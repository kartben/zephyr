.. _memc_api:

Memory Controller (MEMC)
########################

Overview
********

The memory controller (MEMC) driver category covers drivers for external
memory interfaces such as SDRAM, NOR flash, NAND flash, HyperBus, FlexSPI,
and other memory-mapped bus controllers. These drivers configure the on-chip
memory controller hardware so that the attached memory appears as a
directly-addressable memory region to the rest of the system.

Unlike most Zephyr peripheral APIs, the MEMC drivers do not expose a
generic runtime API; they are initialised at boot time and configure the
memory controller registers. Once initialised, the attached memory is
typically accessed directly through its mapped address range or via a
higher-level subsystem (e.g., the :ref:`flash_api` or :ref:`disk_access_api`).

Device-specific MEMC drivers may expose additional APIs for features such
as memory timing calibration or XIP (execute-in-place) configuration.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_MEMC`
* :kconfig:option:`CONFIG_MEMC_INIT_PRIORITY`
