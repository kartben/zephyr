.. _dtdoctor:

Devicetree diagnostics (``dtdoctor``)
#####################################

``dtdoctor`` is a static analysis tool that helps diagnose Devicetree-related build errors.

It intercepts error messages from the compiler and linker and, when they refer to unresolved
Devicetree device symbols (e.g. ``__device_dts_ord_*``), provides detailed information about what
might be causing the error and how to fix it.

Using dtdoctor
**************

To enable ``dtdoctor``, build with ``-DZEPHYR_SCA_VARIANT=dtdoctor``.

For example:

.. code-block:: shell

   west build -b reel_board samples/basic/blinky -- -DZEPHYR_SCA_VARIANT=dtdoctor

What dtdoctor detects
**********************

``dtdoctor`` provides helpful diagnostics for various devicetree-related issues:

* **Missing or disabled nodes**: When a node is referenced but not available, ``dtdoctor``
  shows the node's status and suggests how to enable it.

* **Missing Kconfig options**: When a node is enabled but no driver is available, ``dtdoctor``
  identifies which Kconfig options need to be enabled.

* **Macro usage context**: When nodes are accessed through devicetree macros like ``DT_PARENT()``,
  ``DT_GPARENT()``, ``DT_FOREACH_CHILD()``, ``DT_FOREACH_ANCESTOR()``, or
  ``DT_FOREACH_STATUS_OKAY()``, ``dtdoctor`` provides context about the node relationships and
  suggests which nodes might need to be enabled.

For example, if you use ``DEVICE_DT_GET(DT_PARENT(node_id))`` and the parent node doesn't have a
device defined, ``dtdoctor`` will identify that the problematic node is the parent of other nodes
and suggest enabling it or checking if ``DT_PARENT()`` is being used correctly.
