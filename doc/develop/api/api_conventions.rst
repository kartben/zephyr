.. _api_conventions:

API Conventions
###############

Zephyr provides a few programming interfaces that users can use for creating their applications.
Zephyr's API space includes the following areas:

- C programming interface (function prototypes, structures, and macros), provided in C header files
- Configuration system (Kconfig)
- Hardware description system (Devicetree)

Other areas, such as the build system (CMake), may also be seen as Zephyr's API. However, any area
that is not explicitly listed herein is not considered Zephyr's API as of today.

API classification
******************
The files that define the Zephyr APIs may contain symbols intended for different usage. The intended
API usage is designated by the API class. Zephyr APIs are classified as:

Private
    These APIs are intended for use within the boundary of a :term:`software component`. Private APIs
    defined in the main Zephyr tree are not subject to :ref:`api_lifecycle`. Therefore, they can be changed or
    removed at any time. Changes to the private APIs may not be documented at all, and are not included in the 
    migration guide.

Internal
    In general, these APIs are intended for use only between certain software components that are
    located in the main Zephyr tree. The context where the API is called or implemented is well defined. For
    example, functions prefixed with `arch_` are intended for use by the Zephyr kernel to invoke
    architecture-specific code. An API is classified as internal on a case by case basis.
    Internal APIs should not be used by out-of-tree code. Internal APIs are not subject
    to :ref:`api_lifecycle`. Therefore, they can be changed or removed at any time. However, changes to the
    internal APIs must be documented in the migration guide.

Public
    These APIs intended for use from any :term:`software component`. Public APIs may be used in-tree
    and out-of-tree. Public APIs are subject to the :ref:`api_lifecycle`. Therefore, changes to an
    API are introduced and documented according to the rules defined for the API life cycle. This includes 
    documenting any breaking changes in the migration guide.

Note, that only APIs defined in the main Zephyr tree are subject to :ref:`api_lifecycle`. External projects
used as :ref:`modules` may define their own rules for API lifecycle.

Zephyr is a constantly evolving project and API classification may change over time. A Private or
Internal API may be promoted to Internal or Public API, respectively. Zephyr users are encouraged to
follow :ref:`rfcs` process to recommend changes in API classification.

The following sections provide guidelines on how to identify the class of an API depending on its
type.

Header files contents
=====================
Private
    Functions and data types declared in header files located in
    :zephyr_file:`include/zephyr/private/`. In addition, private symbols are prefixed with ``z_``.
    Due to historical reasons some APIs prefixed with ``z_`` are public.

Internal
    Functions and data types declared in :zephyr_file:`include/zephyr/internal`. In addition, Internal
    APIs must use ``@internal`` doxygen command.

Public
    Functions and data types declared in header files located in :zephyr_file:`include/zephyr/`.

In addition, the following prefixes are reserved by Zephyr kernel for use in Zephyr Public APIs:

.. list-table:: Prefixes and Descriptions
   :header-rows: 1
   :widths: 10 40 40
   :stub-columns: 1

   * - Prefix
     - Description
     - Example
   * - ``atomic_``
     - Denotes an atomic operation.
     - :c:func:`atomic_inc`
   * - ``device_``
     - Denotes an API relating to devices and their initialization.
     - :c:func:`device_get_binding`
   * - ``irq_``
     - Denotes an IRQ management operation.
     - :c:func:`irq_disable`
   * - ``k_``
     - Kernel-specific function.
     - :c:func:`k_malloc`
   * - ``sys_``
     - Catch-all for APIs that do not fit into the other namespaces.
     - :c:func:`sys_write32`

Kconfig symbols
===============
All Kconfig symbols are Public. The :ref:`api_lifecycle` of a Kconfig symbol is defined by the
:ref:`api_lifecycle` of a :term:`software component` to which the binding belongs. For example,
Kconfig symbols defined for regulators follow the lifecycle of the :ref:`regulator_api`.

Devicetree bindings
===================
Device tree bindings and their content are Public. The :ref:`api_lifecycle` of a binding is defined
by the :ref:`api_lifecycle` of a :term:`software component` to which the binding belongs. For
example, bindings defined for regulators follow the lifecycle of the :ref:`regulator_api`.

Breaking and non-breaking changes
**********************************

This section categorizes common changes as breaking or non-breaking for each API type.

.. note::
   This section focuses on API source compatibility. ABI (Application Binary Interface) compatibility
   is out of scope, as it is assumed that applications recompile their entire code base when updating
   Zephyr versions.

.. list-table:: C API Changes
   :header-rows: 1
   :widths: 50 50

   * - Breaking Changes
     - Non-Breaking Changes
   * - | **Functions:**
       | • Remove or rename function
       | • Change return type
       | • Add, remove, or reorder parameters
       | • Change parameter types
       | • Change behavior or error codes
       |
       | **Data types:**
       | • Remove or rename struct/union/enum
       | • Modify struct fields (remove, rename, change type)
       | • Remove/rename enum values
       | • Change enum constant values
       |
       | **Macros:**
       | • Remove or rename macro
       | • Change macro value affecting behavior
     - | **Functions:**
       | • Add new function
       | • Deprecate (not remove) function
       | • Improve documentation
       |
       | **Data types:**
       | • Add new struct/union/enum
       | • Add enum value at end
       | • Add struct field at end (with versioning)
       |
       | **Macros:**
       | • Add new macro

.. list-table:: Kconfig Changes
   :header-rows: 1
   :widths: 50 50

   * - Breaking Changes
     - Non-Breaking Changes
   * - | • Remove or rename symbol
       | • Change symbol type (e.g., bool to int)
       | • Remove default without migration path
       | • Change dependencies breaking configs
       | • Change symbol semantics
     - | • Add new symbol
       | • Change help text or prompt
       | • Change default (backward compatible)
       | • Add ranges/choices including valid values
       | • Deprecate with migration path

.. list-table:: Devicetree Changes
   :header-rows: 1
   :widths: 50 50

   * - Breaking Changes
     - Non-Breaking Changes
   * - | • Remove binding file
       | • Remove or rename required property
       | • Change property type
       | • Make optional property required (no default)
       | • Change phandle-array cell meaning
       | • Remove compatible string
     - | • Add binding file
       | • Add optional property
       | • Add compatible string
       | • Add property default
       | • Make required property optional
       | • Deprecate property (keep support)

API Lifecycle Impact
====================

Breaking changes require different handling based on API classification:

- **Private APIs**: Changes allowed anytime without documentation
- **Internal APIs**: Changes allowed but must document in migration guide
- **Public APIs**: Must follow :ref:`api_lifecycle` (Experimental → Unstable → Stable)
