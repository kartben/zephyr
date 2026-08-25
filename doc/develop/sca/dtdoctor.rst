.. _dtdoctor:

Devicetree diagnostics (``dtdoctor``)
#####################################

``dtdoctor`` is a static analysis tool that helps diagnose Devicetree-related build errors.

It intercepts errors from the compiler and linker and, when they point at Devicetree, works
back to the Devicetree the build actually used to report what is missing and how to fix it.
These are the errors where a Devicetree lookup quietly found nothing, leaving the compiler
to complain about a leftover name, which is what makes them so hard to read unaided.

Using dtdoctor
**************

To enable ``dtdoctor``, build with ``-DZEPHYR_SCA_VARIANT=dtdoctor``.

For example:

.. code-block:: shell

   west build -b reel_board samples/basic/blinky -- -DZEPHYR_SCA_VARIANT=dtdoctor

What dtdoctor can diagnose
**************************

A device that is not there
   Names the node, then reports either that it is disabled (with the file and line where
   that happens, and anything that refers to it) or that no driver is available for it,
   along with the Kconfig options that would enable one.

A node label, alias, ``chosen`` entry or path that does not exist
   Reports which one is missing, and suggests the closest ones in the Devicetree.

A property a node does not have
   Lists the properties it does have, and tells a misspelling apart from a property the
   node's binding declares but the node never sets.

An instance that does not exist
   Reports how many instances of the compatible the build has and which of them are
   enabled, or, when nothing declares that compatible at all, suggests the closest ones.

A bad index, name or cell in a specifier
   Covers any ``phandle-array`` property (``gpios``, ``pwms``, ``clocks``, ``dmas`` and
   the rest), as well as ``interrupts`` and ``reg``. Names the controller the entry points
   at and lists the cells it defines together with this node's values, or lists the entries
   that do exist. Cell names come from the controller's binding rather than from the node
   using it, which is what makes these awkward to track down by hand.

Devicetree names are written differently in C than in DTS: lowercased, with ``-``, ``,``,
``.``, ``@``, ``/`` and ``+`` all becoming ``_``. ``dtdoctor`` spells out both forms
whenever the two differ. See :ref:`dt-use-the-right-names`.

Using the analyzer on its own
*****************************

The analyzer can also be run by hand against a symbol from a build error, without
rebuilding. It needs the :file:`edt.pickle` file from the build directory:

.. code-block:: shell

   ./scripts/dts/dtdoctor_analyzer.py \
       --edt-pickle build/zephyr/edt.pickle \
       --symbol __device_dts_ord_123
