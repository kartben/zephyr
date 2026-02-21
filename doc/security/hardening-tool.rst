.. _hardening:

Hardening Tool
##############

Before launching a product, it's crucial to ensure that your software is as secure as possible. This
process, known as "hardening", involves strengthening the security of a system to protect it from
potential threats and vulnerabilities.

At a high-level, hardening a Zephyr application can be seen as a two-fold process:

#. Disabling features and compilation flags that might lead to security vulnerabilities (ex. making
   sure that no "experimental" features are being used, disabling features typically used for
   debugging purposes such as assertions, shell, etc.).
#. Enabling optional features that can lead to improve security (ex. stack sentinel, hardware stack
   protection, etc.). Some of these features might be hardware-dependent.

To simplify this process, Zephyr offers a **hardening tool** designed to analyze an application's
configuration against a set of hardening preferences defined by the **Security Working Group**. The
tool looks at the Kconfig options in the build target and provides tailored suggestions and
recommendations to adjust security-related options.

The recommended hardened value for each option is stored directly in its Kconfig help text using the
``Hardened value: <value>`` tag. For example:

.. code-block:: kconfig

   config HW_STACK_PROTECTION
   	bool "Hardware Stack Protection"
   	depends on ARCH_HAS_STACK_PROTECTION
   	help
   	  Select this option to enable hardware-based platform features to
   	  catch stack overflows when the system is running in privileged
   	  mode.

   	  Hardened value: y

In addition, any Kconfig option that selects ``EXPERIMENTAL``, ``DEPRECATED``, or ``NOT_SECURE`` is
automatically flagged with a recommended value of ``n``.

Usage
*****

.. zephyr-app-commands::
    :tool: all
    :zephyr-app: samples/hello_world
    :board: reel_board
    :goals: hardenconfig

The output should be similar to the table below. For each configuration option set to a value that
could lead to a security vulnerability, the table will propose a recommended value that should be
used instead.

.. code-block:: console

   +---------------------------------------+-----------+---------------+----------------+
   | Name                                  | Current   | Recommended   | Check result   |
   +=======================================+===========+===============+================+
   | CONFIG_BUILD_OUTPUT_STRIPPED          | n         | y             | FAIL           |
   +---------------------------------------+-----------+---------------+----------------+
   | CONFIG_FAULT_DUMP                     | 2         | 0             | FAIL           |
   +---------------------------------------+-----------+---------------+----------------+
   | CONFIG_MPU_STACK_GUARD                | n         | y             | FAIL           |
   +---------------------------------------+-----------+---------------+----------------+
   | CONFIG_OVERRIDE_FRAME_POINTER_DEFAULT | n         | y             | FAIL           |
   +---------------------------------------+-----------+---------------+----------------+
   | CONFIG_STACK_SENTINEL                 | n         | y             | FAIL           |
   +---------------------------------------+-----------+---------------+----------------+
   | CONFIG_EXCEPTION_DEBUG                | y         | n             | FAIL           |
   +---------------------------------------+-----------+---------------+----------------+
   | CONFIG_PRINTK                         | y         | n             | FAIL           |
   +---------------------------------------+-----------+---------------+----------------+
