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

This section provides guidance on what constitutes a breaking change versus a non-breaking change
for each API type. Understanding these distinctions helps developers maintain API stability and
properly document changes in migration guides.

C API breaking changes
=======================

The following changes to Public or Internal C APIs are considered **breaking changes**:

**Function signatures:**

- Removing a public function
- Renaming a public function
- Changing the return type of a function
- Adding, removing, or reordering function parameters
- Changing parameter types in a way that is not backwards compatible
- Changing a function from inline to non-inline (or vice versa)
- Removing or changing function attributes (e.g., ``__deprecated``, ``__weak``)

**Data types and structures:**

- Removing a public structure, union, or enum
- Renaming a public structure, union, or enum
- Removing, renaming, or reordering fields in a structure or union
- Changing the type of a structure or union field
- Changing the size of a structure or union (except when adding fields at the end with explicit versioning)
- Removing or renaming enum values
- Changing the numeric value of an enum constant

**Macros and constants:**

- Removing a public macro
- Renaming a public macro
- Changing the value of a constant macro in a way that affects behavior
- Changing a function-like macro in a way that is not backwards compatible

**Behavioral changes:**

- Changing the semantics or behavior of a function in a way that breaks existing usage
- Changing error codes returned by a function
- Changing the threading or interrupt context requirements for a function

C API non-breaking changes
===========================

The following changes to Public or Internal C APIs are generally **non-breaking changes**:

- Adding new functions with unique names
- Adding new structures, unions, or enums with unique names
- Adding new enum values (at the end of the enum, without changing existing values)
- Adding fields at the end of a structure when using explicit versioning or size fields
- Adding new macros with unique names
- Deprecating (but not removing) functions, types, or macros using ``__deprecated``
- Adding function attributes that don't change the ABI (e.g., ``__unused``, documentation)
- Changing internal implementation details without affecting the API contract
- Improving documentation or comments
- Adding optional parameters via variadic functions or configuration structures with size/version fields

Kconfig breaking changes
=========================

The following changes to Kconfig symbols are considered **breaking changes**:

- Removing a Kconfig symbol
- Renaming a Kconfig symbol
- Changing the type of a Kconfig symbol (e.g., from bool to int)
- Removing a Kconfig symbol's default value without providing a migration path
- Changing the dependency tree in a way that makes previously valid configurations invalid
- Changing the semantics of what a Kconfig symbol controls

Kconfig non-breaking changes
=============================

The following changes to Kconfig symbols are generally **non-breaking changes**:

- Adding new Kconfig symbols
- Adding new dependencies to an existing symbol (if it doesn't break existing configurations)
- Changing the help text or prompt
- Changing the default value in a way that maintains backwards compatibility
- Adding ranges or choices that include previously valid values
- Deprecating a Kconfig symbol (with migration path to new symbol)

Devicetree breaking changes
============================

The following changes to Devicetree bindings are considered **breaking changes**:

- Removing a binding file
- Removing a required property from a binding
- Renaming a required property in a binding
- Changing the type of a property in a binding
- Removing or changing the meaning of a cell in a phandle-array or phandle property
- Making a previously optional property required without providing a default value
- Removing support for a compatible string

Devicetree non-breaking changes
================================

The following changes to Devicetree bindings are generally **non-breaking changes**:

- Adding new binding files
- Adding new optional properties to a binding
- Adding new compatible strings
- Adding default values for properties
- Making a previously required property optional
- Deprecating a property (while maintaining support for it)
- Changing or improving property descriptions
- Adding new cells to phandle-arrays when properly versioned

API lifecycle considerations
=============================

The impact of a breaking change depends on the API classification:

**Private APIs:**
  Breaking changes are allowed at any time without documentation. Since these APIs are internal
  to a software component, changes are expected and applications should not depend on them.

**Internal APIs:**
  Breaking changes are allowed but must be documented in the migration guide. This helps
  maintainers of other Zephyr components update their code accordingly.

**Public APIs:**
  Breaking changes must follow the :ref:`api_lifecycle`. This typically means:

  - **Experimental APIs**: Can have breaking changes, but should be documented
  - **Unstable APIs**: Can have breaking changes, but must be documented in migration guide
  - **Stable APIs**: Breaking changes should be avoided. If absolutely necessary, must go through
    deprecation period and be thoroughly documented
