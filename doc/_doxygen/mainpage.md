# Zephyr API Reference

This is the reference documentation for the [Zephyr RTOS](https://zephyrproject.org)
C API: the functions, macros, and data structures available to Zephyr
applications and subsystems, generated from the source code.

## Browse the API by topic

The API is organized as a hierarchy of [**Topics**](topics.html), starting from
the following top-level categories:

- @ref kernel_apis — threads, scheduling, synchronization primitives, memory
  allocation, timers, and other kernel services.
- @ref os_services — higher-level services built on top of the kernel, such as
  connectivity (networking, Bluetooth, USB, ...), file systems, power
  management, cryptography, storage, debugging, device management, and the
  shell.
- @ref io_interfaces — hardware-agnostic APIs for interacting with device
  drivers (GPIO, I2C, SPI, sensors, ...) and for implementing new ones.
- @ref device_model — how devices are defined, initialized, and retrieved at
  run time.
- @ref devicetree — macros for accessing the hardware description provided by
  the devicetree.
- @ref mem_mgmt — memory management APIs such as demand paging and memory
  attributes.
- @ref utilities — data structures and general-purpose helper APIs.
- @ref testing — APIs for writing and running tests.
- @ref third_party — APIs to interact with third-party services or
  applications.
- @ref toolchain — macros abstracting compiler and toolchain capabilities.
- @ref system_errno — system error numbers.
- @ref internal_api — internal and system APIs, not intended for direct use by
  applications.

## Other ways to explore

- [Data Structures](annotated.html) — all documented `struct`s and `union`s.
- [Files](files.html) — browse the API by header file.
- [Deprecated List](deprecated.html) — APIs scheduled for removal, and what to
  use instead.
- The search box at the top of every page finds any documented symbol by name.

## Looking for something else?

This site only covers the C API. For everything else — introduction to Zephyr,
getting started guide, developer and contribution guides, kernel and services
documentation, supported boards, samples, release notes, licensing — head over
to the main [Zephyr Project documentation](https://docs.zephyrproject.org/latest/).
