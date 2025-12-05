zephyr_iterable_section
-----------------------

.. cmake:signature::
   zephyr_iterable_section(NAME <name> [GROUP <group>]
                          [VMA <region|group>] [LMA <region|group>]
                          [ADDRESS <address>] [ALIGN_WITH_INPUT]
                          [NUMERIC] [SUBALIGN <alignment>]
   )

   Define an output section which will set up an iterable area
   of equally-sized data structures. For use with STRUCT_SECTION_ITERABLE.
   Input sections will be sorted by name in lexicographical order.

   Each list for an input section will define the following linker symbols:
   - ``_<name>_list_start``: Start of the iterable list
   - ``_<name>_list_end``: End of the iterable list

   The output section will be named `<name>_area` and define the following linker
   symbols:

   +-----------------------------------+-----------------------------------------------------------+
   | Symbol                            | Description                                               |
   +-----------------------------------+-----------------------------------------------------------+
   | ``__<name>_area_start``           | Start address of the section                              |
   +-----------------------------------+-----------------------------------------------------------+
   | ``__<name>_area_end``             | End address of the section                                |
   +-----------------------------------+-----------------------------------------------------------+
   | ``__<name>_area_size``            | Size of the section                                       |
   +-----------------------------------+-----------------------------------------------------------+
   | ``__<name>_area_load_start``      | Load address of the section, if VMA = LMA then this       |
   |                                   | value will be identical to `__<name>_area_start`          |
   +-----------------------------------+-----------------------------------------------------------+

   The options are:

   ``NAME <name>``
     Name of the struct type, the output section will be named
     accordingly: <name>_area.

   ``VMA <region|group>``
     VMA Memory region where code / data is located runtime (VMA)

   ``LMA <region|group>``
     Memory region where code / data is loaded (LMA)
     If VMA is different from LMA, the code / data will be loaded from LMA into VMA at bootup, this
     is usually the case for global or static variables that are loaded in rom and copied to ram at
     boot time.

   ``GROUP <group>``
     Place this section inside the group <group>

   ``ADDRESS <address>``
     Specific address to use for this section.

   ``ALIGN_WITH_INPUT``
     The alignment difference between VMA and LMA is kept
     intact for this section.

   ``NUMERIC``
     Use numeric sorting.

   ``SUBALIGN <alignment>``
     Force input alignment with size <alignment>

   .. note:: Regarding all alignment attributes. Not all linkers may handle alignment
             in identical way. For example the Scatter file will align both load and
             execution address (LMA and VMA) to be aligned when given the ALIGN attribute.
