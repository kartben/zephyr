.. _power_domain_api:

Power Domain
############

Overview
********

Power domains represent controllable power rails that supply one or more
peripherals. The power domain driver layer integrates with the Zephyr
:ref:`pm-device` framework to automatically sequence power on a domain when
any of its child devices is brought up, and to power the domain down when all
child devices are suspended.

A power domain device is declared in devicetree as the ``power-domain``
property of each device that depends on it. The PM framework then tracks
usage counts across all child devices and calls the domain driver's ``pm_action``
callback to assert or deassert the domain power rail.

Examples of power domains include:

- A GPIO-controlled load switch that gates power to a sensor subsystem.
- A PMIC output rail managed via a regulator or an SCMI/TISCI power
  management interface.
- A SoC-internal power gate for a peripheral cluster.

Because power domain management is handled transparently by the device PM
framework, application code does not call power domain functions directly.
Instead, it suspends and resumes the dependent devices using
:c:func:`pm_device_action_run`.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_POWER_DOMAIN`
* :kconfig:option:`CONFIG_POWER_DOMAIN_INIT_PRIORITY`
