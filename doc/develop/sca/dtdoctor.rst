.. _dtdoctor:

Devicetree diagnostics (``dtdoctor``)
#####################################

``dtdoctor`` is a static analysis tool that helps diagnose Devicetree-related build errors.

It intercepts error messages from the compiler and linker and, when they refer to unresolved
Devicetree symbols, provides detailed information about what might be causing the error and how to
fix it.

Detected error classes
**********************

``dtdoctor`` currently recognizes the following classes of errors:

Unresolved device symbols (``__device_dts_ord_*``)
   Typically caused by :c:macro:`DEVICE_DT_GET` on a node that is disabled, or enabled but with no
   driver compiled in. ``dtdoctor`` reports where the node is disabled (and what refers to it), or
   suggests Kconfig options that may be gating the device driver.

Nonexistent node labels and aliases
   E.g. :c:macro:`DT_NODELABEL` or :c:macro:`DT_ALIAS` on a name that does not exist in the final
   devicetree. ``dtdoctor`` suggests close matches among existing labels/aliases and shows how to
   define a missing alias in an overlay.

Missing ``/chosen`` properties
   E.g. :c:macro:`DT_CHOSEN` on a property that is not set. ``dtdoctor`` suggests close matches
   among existing chosen properties and shows how to define one in an overlay.

Nonexistent node paths
   E.g. :c:macro:`DT_PATH` with a path that does not exist. ``dtdoctor`` reports the closest
   existing ancestor node, its children, and similar existing paths.

Missing or mistyped properties
   E.g. :c:macro:`DT_PROP` for a property that is not set on the node. If the property is declared
   in the node's binding, ``dtdoctor`` shows its type and description and how to set it in an
   overlay; otherwise it suggests close matches among the node's properties. Out-of-range array
   indexes (e.g. :c:macro:`DT_PROP_BY_IDX` past the end of an array) are also detected.

Invalid instance numbers
   E.g. :c:macro:`DT_INST` with an instance number that is out of range, or a compatible that no
   node matches. ``dtdoctor`` reports the valid instance numbers, and points out nodes that have
   the right compatible but are not enabled.

Using dtdoctor
**************

To enable ``dtdoctor``, build with ``-DZEPHYR_SCA_VARIANT=dtdoctor``.

For example:

.. code-block:: shell

   west build -b reel_board samples/basic/blinky -- -DZEPHYR_SCA_VARIANT=dtdoctor

Standalone usage
****************

The underlying analyzer can also be run directly against the ``edt.pickle`` file of an existing
build, passing a symbol that appeared in a build error message:

.. code-block:: shell

   ./scripts/dts/dtdoctor_analyzer.py \
       --edt-pickle ./build/zephyr/edt.pickle \
       --symbol __device_dts_ord_123
