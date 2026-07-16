.. _dtdoctor:

Devicetree diagnostics (``dtdoctor``)
#####################################

``dtdoctor`` is a static analysis tool that helps diagnose Devicetree-related build errors.

It intercepts error messages from the compiler and linker and, when they refer to unresolved
Devicetree symbols, provides detailed information about what might be causing the error and how
to fix it.

Supported symbol types include:

- Device ordinal symbols (e.g. ``__device_dts_ord_123``)
- Node label property symbols (e.g. ``DT_N_NODELABEL_vext_P_gpios_IDX_0_VAL_pin``)
- Alias property symbols (e.g. ``DT_N_ALIAS_myalias_P_reg_IDX_0``)
- Instance property symbols (e.g. ``DT_N_INST_0_vendor_device_P_interrupts``)

Using dtdoctor
**************

To enable ``dtdoctor``, build with ``-DZEPHYR_SCA_VARIANT=dtdoctor``.

For example:

.. code-block:: shell

   west build -b reel_board samples/basic/blinky -- -DZEPHYR_SCA_VARIANT=dtdoctor

What dtdoctor diagnoses
***********************

When a build error references an undefined Devicetree symbol, ``dtdoctor`` will:

- Identify the referenced devicetree node (by label, alias, or instance number)
- Check if the node exists in the devicetree
- For property-related symbols, verify the property exists and check array indices
- For phandle-array properties, validate cell names
- Provide helpful suggestions for fixing the error

For example, if you reference a property that doesn't exist, ``dtdoctor`` will show you
what properties are available on that node.
