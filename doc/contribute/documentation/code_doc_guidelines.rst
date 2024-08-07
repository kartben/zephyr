.. _code_doc_guidelines:

Code Documentation Guidelines
#############################

The Zephyr Project is primarily written in C, and it is expected that all
:ref:`public API <api_overview>` are documented.

The Zephyr Project uses Doxygen to generate documentation from specially formatted comments in the
source code. This document provides syntax and style guidelines for writing good Doxygen comments.

General syntax recommendations
******************************

* Comment blocks should use the JavaDoc style, which means that all comment blocks start with
  ``/**`` and end with ``*/``.

* All the comment blocks should start with a brief description of the entity being documented. This
  brief description should be a single sentence that summarizes the purpose of the entity. The use
  of the ``@brief`` tag is not required as the first sentence (up to the first period) is always
  considered the brief description.

* Unless the entity being documented is self-explanatory, the comment block should also include a
  more detailed description of the entity. The use of the ``@details`` tag is not required as the
  detailed description is automatically generated from the text following the brief description.

* Both brief and detailed descriptions should be written in complete sentences and should be
  grammatically correct.

Documenting functions
---------------------

* All function parameters should be documented using the ``@param`` tag. The parameter name should
  be followed by a description of the parameter.

* The return value of a function should be documented using the ``@return`` tag. The description
   should explain what the function returns.

   * The xxx

Available Doxygen aliases
*************************

A Doxygen alias is a custom tag that can typically be used as a shorthand for a longer tag. The
Zephyr Project defines the following aliases:

kconfig
   The ``kconfig`` alias is used to reference Kconfig options in the documentation. It takes exactly
   one argument, which is the name of the Kconfig option. Example:

      .. code-block:: c

         /**
          * @brief This function does bar
          */
         void foo(void);

funcprops
   The ``funcprops`` alias is used to document some of the properties of a function in a real-time
   context. All the properties enumerated in :ref:`api_terms` may be used.

   The following example documents a function that may cause the current thread to sleep and that
   may be invoked before the kernel is fully initialized:

      .. code-block:: c

         /**
          * @brief This function does bar
          *
          * @funcprops @pre_kernel_ok, @sleep
          */
         void foo(void);



Recap
*****

Here's an example of a fully documented header file:


.. code-block:: c
   :caption: include/zephyr/foo.h

   /**
    * @file
    * @brief Brief description of the file.
    *
    * Detailed description of the file.
    */

   #ifndef ZEPHYR_INCLUDE_ZEPHYR_FOO_H_
   #define ZEPHYR_INCLUDE_ZEPHYR_FOO_H_

   #ifdef __cplusplus
   extern "C" {
   #endif

   /**
    * @brief Brief description of the structure.
    *
    * Detailed description of the structure.
    */
   struct foo {
       int public_member; /**< Public member. */
       /** @cond INTERNAL_HIDDEN */
       int internal_member;
       /** @endcond */
   };

   /**
    * @brief Brief description of the function.
    *
    * Detailed description of the function.
    *
    * @param arg1 Description of the first argument.
    * @param arg2 Description of the second argument.
    *
    * @return Description of the return value.
    */
   int foo_function(int arg1, int arg2);

   #ifdef __cplusplus
   }
   #endif

   #endif /* ZEPHYR_INCLUDE_ZEPHYR_FOO_H_ */


Internal API
============

Public headers might include internal members that are required for the implementation but are not
part of the public API. These members should be flagged as such so that they don't appear in the
generated documentation and it is clear that they are not intended for external use.

Internal members must be enclosed in a conditional section delimited by ``@cond INTERNAL_HIDDEN`` /
``@endcond`` tags. For example:

.. code-block:: c

   /**
   * @brief This is a brief description of the structure.
   *
   * This is a more detailed description of the structure.
   */
   struct foo {
       int public_member; /**< Public member. */

       /** @cond INTERNAL_HIDDEN */
       int internal_member;
       /** @endcond */
   };



External References
*******************

- `Doxygen Manual <https://www.doxygen.nl/manual/index.html>`_
- `@cond command <https://www.doxygen.nl/manual/commands.html#cmdcond>`_
